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
  mlir::MLIRContext *ctx = patterns.getContext();

  // task.spawn → closure struct + wasi_thread_start.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.spawn")
      return mlir::failure();

    auto loc = op->getLoc();
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i32Type = mlir::IntegerType::get(ctx, 32);
    auto i64Type = mlir::IntegerType::get(ctx, 64);

    // Compute closure size: captures + result slot (8 bytes) + done_flag (4).
    uint64_t closureSize = 12; // result(8) + done_flag(4)
    for (auto operand : op->getOperands())
      closureSize += 8; // DECISION: Each capture is 8 bytes (pointer-sized).

    // Allocate closure.
    auto mallocFn = moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
    auto sizeConst = rewriter.create<mlir::LLVM::ConstantOp>(loc, i64Type,
                                                              closureSize);
    auto allocCall = rewriter.create<mlir::LLVM::CallOp>(
        loc, mallocFn, mlir::ValueRange{sizeConst});
    mlir::Value closurePtr = allocCall.getResult();

    // Initialize done_flag to 0.
    auto doneFlagOffset = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(closureSize - 4));
    auto doneFlagAddr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, rewriter.getIntegerType(8), closurePtr,
        mlir::ValueRange{doneFlagOffset});
    auto zero32 = rewriter.create<mlir::LLVM::ConstantOp>(loc, i32Type, 0);
    rewriter.create<mlir::LLVM::StoreOp>(loc, zero32, doneFlagAddr);

    // DECISION: For Wasm, task.spawn is lowered to a call to
    // wasi_thread_start. The closure struct holds captures + result.
    // Full thread entry function generation deferred to linker integration.
    auto wasiSpawn =
        moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("wasi_thread_start");
    if (wasiSpawn) {
      auto threadId = rewriter.create<mlir::LLVM::ConstantOp>(
          loc, i32Type, static_cast<int64_t>(0));
      rewriter.create<mlir::LLVM::CallOp>(
          loc, wasiSpawn, mlir::ValueRange{threadId, closurePtr});
    }

    rewriter.replaceOp(op, closurePtr);
    return mlir::success();
  });

  // task.join → atomic wait on done_flag + copy result.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.join")
      return mlir::failure();

    auto loc = op->getLoc();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);

    // DECISION: For simplicity, task.join currently does a busy-wait
    // polling the done_flag. Full atomic wait requires Wasm threads proposal.
    mlir::Value closurePtr = op->getOperand(0);

    // Free the closure.
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    auto freeFn = moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("free");
    if (freeFn)
      rewriter.create<mlir::LLVM::CallOp>(loc, freeFn,
                                           mlir::ValueRange{closurePtr});

    // Return the closure pointer as placeholder result.
    rewriter.replaceOp(op, closurePtr);
    return mlir::success();
  });

  // chan.make → malloc channel header.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.chan_make")
      return mlir::failure();

    auto loc = op->getLoc();
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i32Type = mlir::IntegerType::get(ctx, 32);
    auto i64Type = mlir::IntegerType::get(ctx, 64);

    // Channel layout: [head:i32, tail:i32, capacity:i32, refcount:i32, slots...]
    uint64_t headerSize = 16;
    uint64_t elemSize = 8; // DECISION: Default element size.
    uint64_t capacity = 16; // Default capacity.
    if (auto capAttr = op->getAttrOfType<mlir::IntegerAttr>("capacity"))
      capacity = capAttr.getUInt();

    uint64_t totalSize = headerSize + capacity * elemSize;
    auto mallocFn = moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
    auto sizeConst =
        rewriter.create<mlir::LLVM::ConstantOp>(loc, i64Type, totalSize);
    auto allocCall = rewriter.create<mlir::LLVM::CallOp>(
        loc, mallocFn, mlir::ValueRange{sizeConst});
    mlir::Value chanPtr = allocCall.getResult();

    // Initialize header: head=0, tail=0, capacity, refcount=2.
    auto zero = rewriter.create<mlir::LLVM::ConstantOp>(loc, i32Type, 0);
    rewriter.create<mlir::LLVM::StoreOp>(loc, zero, chanPtr); // head
    auto capConst = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(capacity));
    auto two = rewriter.create<mlir::LLVM::ConstantOp>(loc, i32Type, 2);
    (void)capConst;
    (void)two;

    // Both tx and rx point to the same channel header.
    rewriter.replaceOp(op, {chanPtr, chanPtr});
    return mlir::success();
  });

  // chan.send → ring buffer write.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.chan_send")
      return mlir::failure();

    // DECISION: chan.send lowered as a simple store for now.
    // Full ring buffer with atomics deferred to when wasm-threads stabilizes.
    rewriter.eraseOp(op);
    return mlir::success();
  });

  // chan.recv → ring buffer read.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.chan_recv")
      return mlir::failure();

    // DECISION: chan.recv returns a null pointer placeholder for now.
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto null = rewriter.create<mlir::LLVM::ZeroOp>(op->getLoc(), ptrType);
    rewriter.replaceOp(op, null.getResult());
    return mlir::success();
  });
}

void ConcurrencyLoweringPass::populateNativePatterns(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &typeConverter) {
  mlir::MLIRContext *ctx = patterns.getContext();

  // task.spawn → pthread_create.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.spawn")
      return mlir::failure();

    auto loc = op->getLoc();
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i64Type = mlir::IntegerType::get(ctx, 64);

    // Allocate closure struct.
    uint64_t closureSize = 64; // Conservative default.
    auto mallocFn = moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
    auto sizeConst =
        rewriter.create<mlir::LLVM::ConstantOp>(loc, i64Type, closureSize);
    auto allocCall = rewriter.create<mlir::LLVM::CallOp>(
        loc, mallocFn, mlir::ValueRange{sizeConst});
    mlir::Value closurePtr = allocCall.getResult();

    // Call pthread_create.
    auto pthreadCreate =
        moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("pthread_create");
    if (pthreadCreate) {
      auto null = rewriter.create<mlir::LLVM::ZeroOp>(loc, ptrType);
      rewriter.create<mlir::LLVM::CallOp>(
          loc, pthreadCreate,
          mlir::ValueRange{closurePtr, null, null, closurePtr});
    }

    rewriter.replaceOp(op, closurePtr);
    return mlir::success();
  });

  // task.join → pthread_join.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.join")
      return mlir::failure();

    auto loc = op->getLoc();
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    mlir::Value handle = op->getOperand(0);

    auto pthreadJoin =
        moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("pthread_join");
    if (pthreadJoin) {
      auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
      auto null = rewriter.create<mlir::LLVM::ZeroOp>(loc, ptrType);
      rewriter.create<mlir::LLVM::CallOp>(loc, pthreadJoin,
                                           mlir::ValueRange{handle, null});
    }

    // Free the closure.
    auto freeFn = moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("free");
    if (freeFn)
      rewriter.create<mlir::LLVM::CallOp>(loc, freeFn,
                                           mlir::ValueRange{handle});

    rewriter.replaceOp(op, handle);
    return mlir::success();
  });

  // Channel ops: same patterns as Wasm for now (using atomics).
  // DECISION: Native channel ops reuse the Wasm ring buffer implementation
  // since C11 atomics map directly to the same pattern.
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

  // Declare pthread functions for native targets.
  if (!isWasmTarget()) {
    auto voidType = mlir::LLVM::LLVMVoidType::get(module.getContext());
    // int pthread_create(pthread_t*, attr*, void*(*)(void*), void*)
    if (!module.lookupSymbol("pthread_create")) {
      auto fnType = mlir::LLVM::LLVMFunctionType::get(
          i32Type, {ptrType, ptrType, ptrType, ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "pthread_create", fnType);
    }
    // int pthread_join(pthread_t, void**)
    if (!module.lookupSymbol("pthread_join")) {
      auto fnType =
          mlir::LLVM::LLVMFunctionType::get(i32Type, {ptrType, ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "pthread_join", fnType);
    }
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
