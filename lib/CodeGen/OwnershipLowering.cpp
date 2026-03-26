// OwnershipLowering — converts own.* and borrow.* ops to LLVM dialect.
//
// DECISION: Uses a simple walk-and-replace approach instead of MLIR's
// ConversionTarget/RewritePatternSet framework. This avoids LLVM 18
// API incompatibilities with lambda-based pattern registration.

#include "asc/CodeGen/OwnershipLowering.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {
namespace {

static mlir::LLVM::LLVMFuncOp
getOrInsertMalloc(mlir::ModuleOp module, mlir::OpBuilder &builder) {
  auto fn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
  if (fn) return fn;
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  auto i64 = mlir::IntegerType::get(module.getContext(), 64);
  auto ptr = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto ty = mlir::LLVM::LLVMFunctionType::get(ptr, {i64});
  return builder.create<mlir::LLVM::LLVMFuncOp>(module.getLoc(), "malloc", ty);
}

static mlir::LLVM::LLVMFuncOp
getOrInsertFree(mlir::ModuleOp module, mlir::OpBuilder &builder) {
  auto fn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("free");
  if (fn) return fn;
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  auto ptr = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto voidTy = mlir::LLVM::LLVMVoidType::get(module.getContext());
  auto ty = mlir::LLVM::LLVMFunctionType::get(voidTy, {ptr});
  return builder.create<mlir::LLVM::LLVMFuncOp>(module.getLoc(), "free", ty);
}

struct OwnershipLoweringPass
    : public mlir::PassWrapper<OwnershipLoweringPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OwnershipLoweringPass)

  llvm::StringRef getArgument() const override {
    return "asc-ownership-lowering";
  }
  llvm::StringRef getDescription() const override {
    return "Lower own.* and borrow.* ops to LLVM dialect";
  }

  void runOnOperation() override {
    auto module = getOperation();
    mlir::OpBuilder builder(module.getContext());

    llvm::SmallVector<mlir::Operation *, 32> opsToLower;
    module.walk([&](mlir::Operation *op) {
      llvm::StringRef name = op->getName().getStringRef();
      if (name.starts_with("own."))
        opsToLower.push_back(op);
    });

    auto *ctx = module.getContext();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i64Type = mlir::IntegerType::get(ctx, 64);

    for (auto *op : opsToLower) {
      builder.setInsertionPoint(op);
      llvm::StringRef name = op->getName().getStringRef();
      auto loc = op->getLoc();

      if (name == "own.alloc") {
        uint64_t size = 8;
        if (auto sizeAttr = op->getAttrOfType<mlir::IntegerAttr>("size"))
          size = sizeAttr.getUInt();
        // Stack allocation by default.
        auto i8Ty = mlir::IntegerType::get(ctx, 8);
        auto arrayTy = mlir::LLVM::LLVMArrayType::get(i8Ty, size);
        auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
        auto alloca = builder.create<mlir::LLVM::AllocaOp>(loc, ptrType, arrayTy, one);
        if (op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(alloca.getResult());
        op->erase();
      } else if (name == "own.move" || name == "own.copy" ||
                 name == "own.borrow_ref" || name == "own.borrow_mut") {
        // Forward SSA value.
        if (op->getNumOperands() > 0 && op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(op->getOperand(0));
        op->erase();
      } else if (name == "own.drop") {
        if (op->getNumOperands() > 0) {
          auto val = op->getOperand(0);
          bool needsFree = true;
          if (auto *defOp = val.getDefiningOp())
            if (mlir::isa<mlir::LLVM::AllocaOp>(defOp))
              needsFree = false;
          if (needsFree) {
            auto freeFn = getOrInsertFree(module, builder);
            builder.create<mlir::LLVM::CallOp>(loc, freeFn, mlir::ValueRange{val});
          }
        }
        op->erase();
      } else if (name == "own.try_scope" || name == "own.catch_scope" ||
                 name == "own.cleanup_scope") {
        op->erase();
      } else if (name == "own.rethrow" || name == "own.resume") {
        builder.create<mlir::LLVM::UnreachableOp>(loc);
        op->erase();
      } else {
        op->erase();
      }
    }

    // --- Phase 2: Rewrite function and call signatures ---
    // Any own-dialect types remaining in func.func signatures or
    // func.call result types must be lowered to !llvm.ptr.
    auto isOwnDialectType = [](mlir::Type t) -> bool {
      return mlir::isa<own::OwnValType>(t) ||
             mlir::isa<own::BorrowType>(t) ||
             mlir::isa<own::BorrowMutType>(t) ||
             mlir::isa<mlir::NoneType>(t);  // Self/void params
    };

    // Rewrite func.func signatures.
    module.walk([&](mlir::func::FuncOp funcOp) {
      auto funcType = funcOp.getFunctionType();
      bool needsUpdate = false;

      llvm::SmallVector<mlir::Type> newInputs;
      for (auto t : funcType.getInputs()) {
        if (isOwnDialectType(t)) {
          newInputs.push_back(ptrType);
          needsUpdate = true;
        } else {
          newInputs.push_back(t);
        }
      }

      llvm::SmallVector<mlir::Type> newResults;
      for (auto t : funcType.getResults()) {
        if (isOwnDialectType(t)) {
          newResults.push_back(ptrType);
          needsUpdate = true;
        } else {
          newResults.push_back(t);
        }
      }

      if (needsUpdate) {
        auto newType = mlir::FunctionType::get(ctx, newInputs, newResults);
        funcOp.setType(newType);
        // Update block argument types to match.
        if (!funcOp.isDeclaration()) {
          for (unsigned i = 0; i < funcOp.getNumArguments(); ++i) {
            if (isOwnDialectType(funcOp.getArgument(i).getType()))
              funcOp.getArgument(i).setType(ptrType);
          }
        }
      }
    });

    // Rewrite func.call result types.
    module.walk([&](mlir::func::CallOp callOp) {
      for (unsigned i = 0; i < callOp.getNumResults(); ++i) {
        if (isOwnDialectType(callOp.getResult(i).getType()))
          callOp.getResult(i).setType(ptrType);
      }
    });
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createOwnershipLoweringPass() {
  return std::make_unique<OwnershipLoweringPass>();
}

} // namespace asc
