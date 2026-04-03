// PanicScopeWrap — wraps scopes containing panicking ops in try/catch.
//
// Implements panic safety from RFC-0009: scopes with owned resources and
// potentially-panicking operations are wrapped in EH constructs so that
// destructors run on unwind.

#include "asc/Analysis/PanicScopeWrap.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static bool isOwnedType(mlir::Type type) {
  // Check for own.val custom dialect type.
  llvm::StringRef typeName = type.getAbstractType().getName();
  if (typeName.contains("own.val"))
    return true;
  // Also detect LLVM pointer types — struct/heap allocations are owned.
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(type))
    return true;
  return false;
}

/// Check if a specific value represents an owned resource.
static bool isOwnedValue(mlir::Value value) {
  if (auto *defOp = value.getDefiningOp()) {
    // Alloca of struct type → stack-owned resource needing cleanup.
    if (auto allocaOp = mlir::dyn_cast<mlir::LLVM::AllocaOp>(defOp)) {
      if (auto elemType = allocaOp.getElemType()) {
        if (mlir::isa<mlir::LLVM::LLVMStructType>(elemType))
          return true;
      }
    }
    // malloc calls → heap-owned resource.
    if (auto callOp = mlir::dyn_cast<mlir::LLVM::CallOp>(defOp)) {
      if (auto callee = callOp.getCallee()) {
        if (*callee == "malloc" || callee->starts_with("__asc_"))
          return true;
      }
    }
  }
  return isOwnedType(value.getType());
}

bool PanicScopeWrapPass::canPanic(mlir::Operation *op) const {
  llvm::StringRef opName = op->getName().getStringRef();

  // Operations that can panic:
  // - Array/slice index operations (bounds check).
  if (opName == "own.index" || opName == "own.slice")
    return true;

  // - Integer arithmetic with overflow checking.
  if (opName == "own.checked_add" || opName == "own.checked_sub" ||
      opName == "own.checked_mul" || opName == "own.checked_div")
    return true;

  // - Explicit panic.
  if (opName == "own.panic")
    return true;

  // - Unwrap on Option/Result.
  if (opName == "own.unwrap")
    return true;

  // - Division (div by zero).
  if (opName == "arith.divsi" || opName == "arith.divui" ||
      opName == "arith.divf")
    return true;

  // - Function calls (callee may panic).
  if (opName == "func.call")
    return true;

  // - Assert operations.
  if (opName == "own.assert")
    return true;

  return false;
}

bool PanicScopeWrapPass::isNoPanicScope(mlir::Operation *scopeOp) const {
  // Walk the scope's regions looking for any panicking operation.
  bool foundPanic = false;
  scopeOp->walk([&](mlir::Operation *op) {
    if (canPanic(op))
      foundPanic = true;
  });
  return !foundPanic;
}

//===----------------------------------------------------------------------===//
// Scope identification
//===----------------------------------------------------------------------===//

void PanicScopeWrapPass::identifyScopes(mlir::func::FuncOp func) {
  scopes.clear();

  // The function body itself is a scope.
  ScopeInfo funcScope;
  funcScope.scopeOp = func.getOperation();

  // Find panic points in the function.
  func.walk([&](mlir::Operation *op) {
    if (canPanic(op)) {
      funcScope.panicPoints.push_back(op);
    }
  });

  if (!funcScope.panicPoints.empty()) {
    scopes.push_back(std::move(funcScope));
  }

  // Also identify nested scopes (regions within operations like loops,
  // if/else, match, etc.) that contain both owned values and panic points.
  func.walk([&](mlir::Operation *op) {
    // Skip the function itself (already handled).
    if (op == func.getOperation())
      return;

    // Only consider operations with regions (scopes).
    if (op->getNumRegions() == 0)
      return;

    ScopeInfo scope;
    scope.scopeOp = op;

    // Find panic points within this scope's regions.
    op->walk([&](mlir::Operation *inner) {
      if (inner != op && canPanic(inner)) {
        scope.panicPoints.push_back(inner);
      }
    });

    if (!scope.panicPoints.empty()) {
      scopes.push_back(std::move(scope));
    }
  });
}

//===----------------------------------------------------------------------===//
// Liveness across panic points
//===----------------------------------------------------------------------===//

void PanicScopeWrapPass::computeLiveAcrossPanic(ScopeInfo &scope) {
  scope.liveOwnedValues.clear();

  // Collect all owned values defined before any panic point and used after.
  // Simplified: collect all owned values in the scope that are defined
  // before the first panic point.
  llvm::DenseSet<mlir::Value> ownedValues;
  llvm::DenseSet<mlir::Value> definedBeforePanic;

  if (scope.panicPoints.empty()) {
    scope.needsWrap = false;
    return;
  }

  mlir::Operation *firstPanic = scope.panicPoints.front();

  // Walk the scope to find owned values defined before the first panic.
  scope.scopeOp->walk([&](mlir::Operation *op) {
    // Skip operations in nested scopes that have their own wrapping.
    if (op == scope.scopeOp)
      return;

    for (mlir::Value result : op->getResults()) {
      if (isOwnedValue(result)) {
        ownedValues.insert(result);

        // Check if this definition is before the first panic point.
        if (op->getBlock() == firstPanic->getBlock() &&
            op->isBeforeInBlock(firstPanic)) {
          definedBeforePanic.insert(result);
        }
      }
    }
  });

  // Also include function arguments that are owned.
  if (auto funcOp =
          mlir::dyn_cast<mlir::func::FuncOp>(scope.scopeOp)) {
    for (mlir::Value arg :
         funcOp.getBody().front().getArguments()) {
      if (isOwnedValue(arg)) {
        definedBeforePanic.insert(arg);
      }
    }
  }

  // An owned value needs cleanup if it is live across a panic point:
  // defined before and not consumed before the panic.
  for (mlir::Value val : definedBeforePanic) {
    bool consumedBeforePanic = false;
    for (mlir::OpOperand &use : val.getUses()) {
      mlir::Operation *useOp = use.getOwner();
      llvm::StringRef useName = useOp->getName().getStringRef();
      if (useName == "own.drop" || useName == "own.move") {
        if (useOp->getBlock() == firstPanic->getBlock() &&
            useOp->isBeforeInBlock(firstPanic)) {
          consumedBeforePanic = true;
          break;
        }
      }
    }
    if (!consumedBeforePanic) {
      scope.liveOwnedValues.push_back(val);
    }
  }

  scope.needsWrap = !scope.liveOwnedValues.empty();
}

//===----------------------------------------------------------------------===//
// Scope wrapping
//===----------------------------------------------------------------------===//

void PanicScopeWrapPass::buildCleanupBlock(
    mlir::OpBuilder &builder,
    const llvm::SmallVector<mlir::Value, 8> &values) {
  mlir::Location loc = builder.getUnknownLoc();

  // Insert drops for all live owned values in reverse order (LIFO).
  for (auto it = values.rbegin(); it != values.rend(); ++it) {
    mlir::OperationState state(loc, "own.drop");
    state.addOperands(*it);
    builder.create(state);
  }
}

void PanicScopeWrapPass::wrapScope(ScopeInfo &scope) {
  if (!scope.needsWrap)
    return;

  mlir::OpBuilder builder(scope.scopeOp);
  mlir::Location loc = scope.scopeOp->getLoc();

  // Strategy depends on whether this is a function or nested scope.

  if (isWasmTarget) {
    // Wasm EH: Wrap with own.try_scope { ... } own.catch_scope { cleanup }
    //
    // The own.try_scope operation contains the original body.
    // The own.catch_scope operation contains the cleanup (drops).
    //
    // After lowering, this becomes:
    //   try
    //     <original code>
    //   catch $__asc_panic_tag
    //     <drop all live owned values>
    //     rethrow

    // Create the try_scope operation wrapping the panic region.
    {
      mlir::OperationState tryState(loc, "own.try_scope");
      tryState.addRegion(); // Body region (will contain the original ops).
      mlir::Operation *tryOp = builder.create(tryState);

      // The catch/cleanup is represented as a separate operation.
      mlir::OperationState catchState(loc, "own.catch_scope");
      catchState.addRegion(); // Cleanup region.
      mlir::Operation *catchOp = builder.create(catchState);

      // Build the cleanup block inside the catch region.
      if (!catchOp->getRegions().empty()) {
        mlir::Region &cleanupRegion = catchOp->getRegion(0);
        mlir::Block *cleanupBlock = builder.createBlock(&cleanupRegion);
        builder.setInsertionPointToStart(cleanupBlock);
        buildCleanupBlock(builder, scope.liveOwnedValues);

        // Re-throw after cleanup.
        mlir::OperationState rethrowState(loc, "own.rethrow");
        builder.create(rethrowState);
      }
    }
  } else {
    // Native EH: Wrap with landingpad-based EH.
    //
    // The own.try_scope operation lowers to invoke instructions.
    // The own.cleanup_scope operation lowers to a landingpad + cleanup block.

    mlir::OperationState tryState(loc, "own.try_scope");
    tryState.addRegion();
    builder.create(tryState);

    mlir::OperationState cleanupState(loc, "own.cleanup_scope");
    cleanupState.addRegion();
    mlir::Operation *cleanupOp = builder.create(cleanupState);

    if (!cleanupOp->getRegions().empty()) {
      mlir::Region &cleanupRegion = cleanupOp->getRegion(0);
      mlir::Block *cleanupBlock = builder.createBlock(&cleanupRegion);
      builder.setInsertionPointToStart(cleanupBlock);
      buildCleanupBlock(builder, scope.liveOwnedValues);

      // Resume unwinding after cleanup.
      mlir::OperationState resumeState(loc, "own.resume");
      builder.create(resumeState);
    }
  }
}

//===----------------------------------------------------------------------===//
// Pass entry point
//===----------------------------------------------------------------------===//

void PanicScopeWrapPass::runOnOperation() {
  mlir::func::FuncOp func = getOperation();
  if (func.isDeclaration())
    return;

  // Determine target: check if any ancestor module has a wasm target triple.
  isWasmTarget = true; // Default to Wasm.
  if (auto moduleOp = func->getParentOfType<mlir::ModuleOp>()) {
    if (auto tripleAttr = moduleOp->getAttrOfType<mlir::StringAttr>(
            "llvm.target_triple")) {
      llvm::StringRef triple = tripleAttr.getValue();
      isWasmTarget = triple.contains("wasm");
    }
  }

  // Step 1: Identify scopes that contain panicking operations.
  identifyScopes(func);

  // Step 2: For each scope, compute which owned values are live across
  // panic points.
  for (auto &scope : scopes) {
    computeLiveAcrossPanic(scope);
  }

  // Step 3: Wrap scopes that need it.
  // Process innermost scopes first to avoid invalidating outer scopes.
  // Reverse iteration approximates inside-out ordering.
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    wrapScope(*it);
  }
}

std::unique_ptr<mlir::Pass> createPanicScopeWrapPass() {
  return std::make_unique<PanicScopeWrapPass>();
}

} // namespace asc
