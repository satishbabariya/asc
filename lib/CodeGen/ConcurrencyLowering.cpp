// ConcurrencyLowering — declares runtime function symbols for the linker.
//
// task.* ops are lowered inline in HIRBuilder; this pass only ensures
// that runtime symbols (malloc, free, pthread_create, etc.) are declared.

#include "asc/CodeGen/ConcurrencyLowering.h"
#include "asc/HIR/TaskOps.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"

namespace asc {

void ConcurrencyLoweringPass::configureTarget(mlir::ConversionTarget &) {}
void ConcurrencyLoweringPass::populateWasmPatterns(
    mlir::RewritePatternSet &, mlir::TypeConverter &) {}
void ConcurrencyLoweringPass::populateNativePatterns(
    mlir::RewritePatternSet &, mlir::TypeConverter &) {}

void ConcurrencyLoweringPass::runOnOperation() {
  auto module = getOperation();
  mlir::OpBuilder builder(module.getContext());

  // Declare runtime functions.
  declareRuntimeFunctions(module);
  if (isWasmTarget())
    declareWasiThreadsFunctions(module);

  // NOTE: task.spawn, task.join, chan.make/send/recv are lowered inline
  // in HIRBuilder (direct calls to pthread_create, __asc_chan_* runtime).
  // No task.* dialect ops exist in the IR at this point.
  // This pass only declares runtime function symbols for the linker.
}

void ConcurrencyLoweringPass::declareRuntimeFunctions(mlir::ModuleOp module) {
  mlir::OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  auto loc = builder.getUnknownLoc();
  auto ptrType = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto i32Type = mlir::IntegerType::get(module.getContext(), 32);
  auto i64Type = mlir::IntegerType::get(module.getContext(), 64);

  if (!module.lookupSymbol("malloc")) {
    auto ty = mlir::LLVM::LLVMFunctionType::get(ptrType, {i64Type});
    builder.create<mlir::LLVM::LLVMFuncOp>(loc, "malloc", ty);
  }
  if (!module.lookupSymbol("free")) {
    auto voidTy = mlir::LLVM::LLVMVoidType::get(module.getContext());
    auto ty = mlir::LLVM::LLVMFunctionType::get(voidTy, {ptrType});
    builder.create<mlir::LLVM::LLVMFuncOp>(loc, "free", ty);
  }

  // SPSC channel ref-counted drop/clone (available on all targets).
  {
    auto voidTy = mlir::LLVM::LLVMVoidType::get(module.getContext());
    if (!module.lookupSymbol("__asc_chan_drop")) {
      auto fnType = mlir::LLVM::LLVMFunctionType::get(voidTy,
          {ptrType, i32Type, ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_chan_drop", fnType);
    }
    if (!module.lookupSymbol("__asc_chan_clone")) {
      auto fnType = mlir::LLVM::LLVMFunctionType::get(voidTy, {ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_chan_clone", fnType);
    }
  }

  if (!isWasmTarget()) {
    if (!module.lookupSymbol("pthread_create")) {
      auto ty = mlir::LLVM::LLVMFunctionType::get(i32Type,
                                                    {ptrType, ptrType, ptrType, ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "pthread_create", ty);
    }
    if (!module.lookupSymbol("pthread_join")) {
      auto ty = mlir::LLVM::LLVMFunctionType::get(i32Type, {ptrType, ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "pthread_join", ty);
    }

    // MPMC channel functions.
    auto ctx = module.getContext();
    if (!module.lookupSymbol("__asc_mpmc_chan_create")) {
      auto fnType = mlir::LLVM::LLVMFunctionType::get(ptrType, {i32Type, i32Type});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_create", fnType);
    }
    if (!module.lookupSymbol("__asc_mpmc_chan_send")) {
      auto voidType = mlir::LLVM::LLVMVoidType::get(ctx);
      auto fnType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType, ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_send", fnType);
    }
    if (!module.lookupSymbol("__asc_mpmc_chan_recv")) {
      auto fnType = mlir::LLVM::LLVMFunctionType::get(i32Type, {ptrType, ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_recv", fnType);
    }
    if (!module.lookupSymbol("__asc_mpmc_chan_drop")) {
      auto voidType = mlir::LLVM::LLVMVoidType::get(ctx);
      auto fnType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_drop", fnType);
    }
    if (!module.lookupSymbol("__asc_mpmc_chan_clone")) {
      auto voidType = mlir::LLVM::LLVMVoidType::get(ctx);
      auto fnType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_clone", fnType);
    }
  }
}

void ConcurrencyLoweringPass::declareWasiThreadsFunctions(mlir::ModuleOp module) {
  mlir::OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  auto loc = builder.getUnknownLoc();
  auto ptrType = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto i32Type = mlir::IntegerType::get(module.getContext(), 32);
  auto voidTy = mlir::LLVM::LLVMVoidType::get(module.getContext());

  if (!module.lookupSymbol("wasi_thread_start")) {
    auto ty = mlir::LLVM::LLVMFunctionType::get(voidTy, {i32Type, ptrType});
    builder.create<mlir::LLVM::LLVMFuncOp>(loc, "wasi_thread_start", ty);
  }
}

std::unique_ptr<mlir::Pass> createConcurrencyLoweringPass() {
  return std::make_unique<ConcurrencyLoweringPass>();
}

std::unique_ptr<mlir::Pass>
createConcurrencyLoweringPass(llvm::Triple targetTriple) {
  return std::make_unique<ConcurrencyLoweringPass>(std::move(targetTriple));
}

} // namespace asc
