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

        bool useHeap = op->hasAttr("heap");
        // Escape analysis: auto-promote to heap if value escapes scope.
        if (auto escapeAttr = op->getAttrOfType<mlir::StringAttr>("escape_status")) {
          if (escapeAttr.getValue() == "must_heap")
            useHeap = true;
        }
        mlir::Value result;

        if (useHeap) {
          // @heap: allocate on heap via malloc.
          auto mallocFn = getOrInsertMalloc(module, builder);
          auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)size);
          auto callOp = builder.create<mlir::LLVM::CallOp>(loc, mallocFn, mlir::ValueRange{sizeVal});
          result = callOp.getResult();
        } else {
          // Stack allocation by default.
          auto i8Ty = mlir::IntegerType::get(ctx, 8);
          auto arrayTy = mlir::LLVM::LLVMArrayType::get(i8Ty, size);
          auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
          auto alloca = builder.create<mlir::LLVM::AllocaOp>(loc, ptrType, arrayTy, one);
          result = alloca.getResult();
        }

        if (op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(result);
        op->erase();
      } else if (name == "own.move") {
        if (op->getNumOperands() > 0 && op->getNumResults() > 0) {
          auto operand = op->getOperand(0);
          // Check if the operand comes from an alloca of a struct type.
          // If so, memcpy the data to a new allocation.
          bool isAggregate = false;
          uint64_t structSize = 0;
          if (auto *defOp = operand.getDefiningOp()) {
            if (auto allocaOp = mlir::dyn_cast<mlir::LLVM::AllocaOp>(defOp)) {
              if (auto elemType = allocaOp.getElemType()) {
                if (auto structTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(elemType)) {
                  isAggregate = true;
                  // Compute actual struct size by summing field sizes.
                  for (mlir::Type fieldTy : structTy.getBody()) {
                    if (fieldTy.isIntOrIndexOrFloat())
                      structSize += (fieldTy.getIntOrFloatBitWidth() + 7) / 8;
                    else
                      structSize += 8; // Pointer-sized default.
                  }
                  if (structSize == 0) structSize = 8;
                }
              }
            }
          }
          if (isAggregate && structSize > 0) {
            // Allocate destination and memcpy.
            auto i8Ty = mlir::IntegerType::get(ctx, 8);
            auto arrayTy = mlir::LLVM::LLVMArrayType::get(i8Ty, structSize);
            auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
            auto dst = builder.create<mlir::LLVM::AllocaOp>(loc, ptrType, arrayTy, one);
            auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)structSize);
            auto falseCst = builder.create<mlir::LLVM::ConstantOp>(
                loc, mlir::IntegerType::get(ctx, 1), (int64_t)0);
            builder.create<mlir::LLVM::MemcpyOp>(loc, dst, operand, sizeVal, falseCst);
            op->getResult(0).replaceAllUsesWith(dst.getResult());
          } else {
            // Scalar/pointer: SSA forward.
            op->getResult(0).replaceAllUsesWith(operand);
          }
        }
        op->erase();
      } else if (name == "own.copy") {
        // Deep copy for @copy types: allocate new storage and memcpy.
        if (op->getNumOperands() > 0 && op->getNumResults() > 0) {
          auto operand = op->getOperand(0);
          bool isAggregate = false;
          uint64_t structSize = 0;
          if (auto *defOp = operand.getDefiningOp()) {
            if (auto allocaOp = mlir::dyn_cast<mlir::LLVM::AllocaOp>(defOp)) {
              if (auto elemType = allocaOp.getElemType()) {
                if (auto structTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(elemType)) {
                  isAggregate = true;
                  for (mlir::Type fieldTy : structTy.getBody()) {
                    if (fieldTy.isIntOrIndexOrFloat())
                      structSize += (fieldTy.getIntOrFloatBitWidth() + 7) / 8;
                    else
                      structSize += 8;
                  }
                  if (structSize == 0) structSize = 8;
                }
              }
            }
          }
          if (isAggregate && structSize > 0) {
            // Allocate new storage and memcpy.
            auto i8Ty = mlir::IntegerType::get(ctx, 8);
            auto arrayTy = mlir::LLVM::LLVMArrayType::get(i8Ty, structSize);
            auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
            auto dst = builder.create<mlir::LLVM::AllocaOp>(loc, ptrType, arrayTy, one);
            auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)structSize);
            auto falseCst = builder.create<mlir::LLVM::ConstantOp>(
                loc, mlir::IntegerType::get(ctx, 1), (int64_t)0);
            builder.create<mlir::LLVM::MemcpyOp>(loc, dst, operand, sizeVal, falseCst);
            op->getResult(0).replaceAllUsesWith(dst.getResult());
          } else {
            // Scalar: SSA value copy (bitwise copy for primitives).
            op->getResult(0).replaceAllUsesWith(operand);
          }
        }
        op->erase();
      } else if (name == "own.borrow_ref" || name == "own.borrow_mut") {
        // Forward SSA value (borrows don't transfer ownership of data).
        if (op->getNumOperands() > 0 && op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(op->getOperand(0));
        op->erase();
      } else if (name == "own.drop_flag_alloc") {
        // Allocate an i1 flag on the stack, initialized to true (not yet moved).
        auto i1Ty = mlir::IntegerType::get(ctx, 1);
        auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
        auto alloca = builder.create<mlir::LLVM::AllocaOp>(loc, ptrType, i1Ty, one);
        // Initialize to true (value is still alive, should be dropped).
        auto trueCst = builder.create<mlir::LLVM::ConstantOp>(loc, i1Ty, (int64_t)1);
        builder.create<mlir::LLVM::StoreOp>(loc, trueCst, alloca);
        if (op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(alloca.getResult());
        op->erase();
      } else if (name == "own.drop_flag_set") {
        // Store the boolean value into the flag pointer.
        if (op->getNumOperands() >= 2) {
          auto flagPtr = op->getOperand(0);
          auto boolVal = op->getOperand(1);
          builder.create<mlir::LLVM::StoreOp>(loc, boolVal, flagPtr);
        }
        op->erase();
      } else if (name == "own.drop_flag_check") {
        // Load the flag value.
        if (op->getNumOperands() > 0 && op->getNumResults() > 0) {
          auto flagPtr = op->getOperand(0);
          auto i1Ty = mlir::IntegerType::get(ctx, 1);
          auto load = builder.create<mlir::LLVM::LoadOp>(loc, i1Ty, flagPtr);
          op->getResult(0).replaceAllUsesWith(load.getResult());
        }
        op->erase();
      } else if (name == "own.drop") {
        if (op->getNumOperands() > 0) {
          auto val = op->getOperand(0);
          bool needsFree = true;
          if (auto *defOp = val.getDefiningOp())
            if (mlir::isa<mlir::LLVM::AllocaOp>(defOp))
              needsFree = false;

          // Check for drop_flag attribute — conditional drop.
          // If present, the second operand is the drop flag pointer.
          // Only drop if the flag is true (value has not been moved).
          bool hasDropFlag = op->hasAttr("drop_flag") &&
                             op->getNumOperands() >= 2;

          if (hasDropFlag) {
            auto flagPtr = op->getOperand(1);
            auto i1Ty = mlir::IntegerType::get(ctx, 1);
            // Load the drop flag to check if value is still alive (not moved).
            auto flagLoad =
                builder.create<mlir::LLVM::LoadOp>(loc, i1Ty, flagPtr);

            // Split block: check → drop (conditional) → merge (continue).
            mlir::Block *checkBlock = op->getBlock();
            mlir::Block *mergeBlock = checkBlock->splitBlock(op);
            auto *parentRegion = checkBlock->getParent();
            mlir::Block *dropBlock = new mlir::Block();
            parentRegion->getBlocks().insertAfter(
                mlir::Region::iterator(checkBlock), dropBlock);

            // check-block: branch to drop-block if flag is true (still alive),
            // otherwise skip to merge-block.
            builder.setInsertionPointToEnd(checkBlock);
            builder.create<mlir::LLVM::CondBrOp>(loc, flagLoad, dropBlock,
                                                  mergeBlock);

            // drop-block: emit destructor call and free, then branch to merge.
            builder.setInsertionPointToStart(dropBlock);

            // Check for custom Drop destructor.
            if (auto typeNameAttr =
                    op->getAttrOfType<mlir::StringAttr>("type_name")) {
              std::string dropFnName =
                  "__drop_" + typeNameAttr.getValue().str();
              auto dropFn =
                  module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(dropFnName);
              if (!dropFn) {
                if (auto funcDropFn =
                        module.lookupSymbol<mlir::func::FuncOp>(dropFnName)) {
                  builder.create<mlir::func::CallOp>(loc, funcDropFn,
                                                      mlir::ValueRange{val});
                }
              } else {
                builder.create<mlir::LLVM::CallOp>(loc, dropFn,
                                                    mlir::ValueRange{val});
              }
            }

            if (needsFree) {
              auto freeFn = getOrInsertFree(module, builder);
              builder.create<mlir::LLVM::CallOp>(loc, freeFn,
                                                  mlir::ValueRange{val});
            }

            builder.create<mlir::LLVM::BrOp>(loc, mlir::ValueRange{},
                                              mergeBlock);
          } else {
            // Unconditional drop (normal case).

            // Check for custom Drop destructor.
            // Drop methods are emitted as __drop_TypeName by HIRBuilder.
            if (auto typeNameAttr = op->getAttrOfType<mlir::StringAttr>("type_name")) {
              std::string dropFnName = "__drop_" + typeNameAttr.getValue().str();
              auto dropFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(dropFnName);
              if (!dropFn) {
                // Also try as func.func (before FuncToLLVM conversion).
                if (auto funcDropFn = module.lookupSymbol<mlir::func::FuncOp>(dropFnName)) {
                  builder.create<mlir::func::CallOp>(loc, funcDropFn, mlir::ValueRange{val});
                }
              } else {
                builder.create<mlir::LLVM::CallOp>(loc, dropFn, mlir::ValueRange{val});
              }
            }

            if (needsFree) {
              auto freeFn = getOrInsertFree(module, builder);
              builder.create<mlir::LLVM::CallOp>(loc, freeFn, mlir::ValueRange{val});
            }
          }
        }
        op->erase();
      } else if (name == "own.try_scope" || name == "own.catch_scope" ||
                 name == "own.cleanup_scope") {
        // PanicScopeWrap emits these with regions. The setjmp/longjmp
        // infrastructure exists in the runtime but wiring it into MLIR
        // ops with regions requires careful block manipulation.
        // MVP: erase the ops. __asc_panic still prints messages + aborts.
        // The handler registration is in the runtime for future use.
        op->erase();
      } else if (name == "own.rethrow" || name == "own.resume") {
        // After cleanup, clear panic handler and abort.
        auto voidTy = mlir::LLVM::LLVMVoidType::get(ctx);
        auto clearFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__asc_clear_panic_handler");
        if (!clearFn) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToStart(module.getBody());
          auto ty = mlir::LLVM::LLVMFunctionType::get(voidTy, {});
          clearFn = builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_clear_panic_handler", ty);
        }
        builder.create<mlir::LLVM::CallOp>(loc, clearFn, mlir::ValueRange{});

        auto abortFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("abort");
        if (!abortFn) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToStart(module.getBody());
          auto ty = mlir::LLVM::LLVMFunctionType::get(voidTy, {});
          abortFn = builder.create<mlir::LLVM::LLVMFuncOp>(loc, "abort", ty);
        }
        builder.create<mlir::LLVM::CallOp>(loc, abortFn, mlir::ValueRange{});
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
