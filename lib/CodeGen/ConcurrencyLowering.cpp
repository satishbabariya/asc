#include "asc/CodeGen/ConcurrencyLowering.h"
#include "asc/HIR/TaskOps.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Transforms/DialectConversion.h"

namespace asc {

void ConcurrencyLoweringPass::runOnOperation() {
  auto module = getOperation();
  mlir::MLIRContext *ctx = &getContext();

  mlir::ConversionTarget target(*ctx);
  configureTarget(target);

  mlir::TypeConverter typeConverter;
  typeConverter.addConversion([](mlir::Type type) { return type; });
  // Convert task types to LLVM pointer types.
  typeConverter.addConversion([ctx](task::TaskHandleType type) -> mlir::Type {
    return mlir::LLVM::LLVMPointerType::get(ctx);
  });
  typeConverter.addConversion([ctx](task::ChanTxType type) -> mlir::Type {
    return mlir::LLVM::LLVMPointerType::get(ctx);
  });
  typeConverter.addConversion([ctx](task::ChanRxType type) -> mlir::Type {
    return mlir::LLVM::LLVMPointerType::get(ctx);
  });

  mlir::RewritePatternSet patterns(ctx);
  declareRuntimeFunctions(module);

  if (isWasmTarget()) {
    declareWasiThreadsFunctions(module);
    populateWasmPatterns(patterns, typeConverter);
  } else {
    populateNativePatterns(patterns, typeConverter);
  }

  if (failed(applyPartialConversion(module, target, std::move(patterns))))
    signalPassFailure();
}

void ConcurrencyLoweringPass::configureTarget(mlir::ConversionTarget &target) {
  target.addLegalDialect<mlir::LLVM::LLVMDialect>();
  // Mark task dialect ops as illegal (must be converted).
  target.addIllegalOp<task::TaskSpawnOp>();
  target.addIllegalOp<task::TaskJoinOp>();
  target.addIllegalOp<task::ChanMakeOp>();
  target.addIllegalOp<task::ChanSendOp>();
  target.addIllegalOp<task::ChanRecvOp>();
}

void ConcurrencyLoweringPass::populateWasmPatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &typeConverter) {
  // DECISION: Wasm concurrency lowering patterns will be implemented when
  // wasm32-wasi-threads support matures. For now, task ops are lowered
  // to sequential execution (stub implementation).
}

void ConcurrencyLoweringPass::populateNativePatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &typeConverter) {
  // DECISION: Native pthreads patterns will be implemented as a follow-up.
  // For now, task ops are lowered to sequential execution.
}

void ConcurrencyLoweringPass::declareRuntimeFunctions(mlir::ModuleOp module) {
  mlir::OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  auto loc = builder.getUnknownLoc();
  auto ptrType = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto i32Type = mlir::IntegerType::get(module.getContext(), 32);
  auto i64Type = mlir::IntegerType::get(module.getContext(), 64);

  // Declare malloc if not already present.
  if (!module.lookupSymbol("malloc")) {
    auto mallocType = mlir::LLVM::LLVMFunctionType::get(ptrType, {i64Type});
    builder.create<mlir::LLVM::LLVMFuncOp>(loc, "malloc", mallocType);
  }

  // Declare free.
  if (!module.lookupSymbol("free")) {
    auto freeType = mlir::LLVM::LLVMFunctionType::get(
        mlir::LLVM::LLVMVoidType::get(module.getContext()), {ptrType});
    builder.create<mlir::LLVM::LLVMFuncOp>(loc, "free", freeType);
  }
}

void ConcurrencyLoweringPass::declareWasiThreadsFunctions(
    mlir::ModuleOp module) {
  mlir::OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  auto loc = builder.getUnknownLoc();
  auto ptrType = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto i32Type = mlir::IntegerType::get(module.getContext(), 32);

  if (!module.lookupSymbol("wasi_thread_start")) {
    auto fnType = mlir::LLVM::LLVMFunctionType::get(
        mlir::LLVM::LLVMVoidType::get(module.getContext()),
        {i32Type, ptrType});
    builder.create<mlir::LLVM::LLVMFuncOp>(loc, "wasi_thread_start", fnType);
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
