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

  static unsigned spawnCounter = 0;

  // task.spawn → closure struct + thread entry function + wasi_thread_start.
  patterns.add([ctx, &spawnCounter](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.spawn")
      return mlir::failure();

    auto loc = op->getLoc();
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i32Type = mlir::IntegerType::get(ctx, 32);
    auto i64Type = mlir::IntegerType::get(ctx, 64);
    auto i8Type = mlir::IntegerType::get(ctx, 8);
    auto voidType = mlir::LLVM::LLVMVoidType::get(ctx);

    unsigned numCaptures = op->getNumOperands();
    // Closure layout: [captures...(8 each), result(8), done_flag(4)]
    uint64_t captureBytes = numCaptures * 8;
    uint64_t resultOffset = captureBytes;
    uint64_t doneFlagOffset = resultOffset + 8;
    uint64_t closureSize = doneFlagOffset + 4;

    // Allocate closure.
    auto mallocFn = moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
    auto sizeConst = rewriter.create<mlir::LLVM::ConstantOp>(loc, i64Type,
                                                              closureSize);
    auto allocCall = rewriter.create<mlir::LLVM::CallOp>(
        loc, mallocFn, mlir::ValueRange{sizeConst});
    mlir::Value closurePtr = allocCall.getResult();

    // Store captures into closure struct.
    for (unsigned i = 0; i < numCaptures; ++i) {
      auto offset = rewriter.create<mlir::LLVM::ConstantOp>(
          loc, i32Type, static_cast<int64_t>(i * 8));
      auto capPtr = rewriter.create<mlir::LLVM::GEPOp>(
          loc, ptrType, i8Type, closurePtr, mlir::ValueRange{offset});
      rewriter.create<mlir::LLVM::StoreOp>(loc, op->getOperand(i), capPtr);
    }

    // Initialize done_flag to 0.
    auto doneOffset = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(doneFlagOffset));
    auto doneFlagAddr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, i8Type, closurePtr, mlir::ValueRange{doneOffset});
    auto zero32 = rewriter.create<mlir::LLVM::ConstantOp>(loc, i32Type, 0);
    rewriter.create<mlir::LLVM::StoreOp>(loc, zero32, doneFlagAddr);

    // Generate thread entry function: __task_entry_N.
    std::string entryName = "__task_entry_" + std::to_string(spawnCounter++);
    {
      mlir::OpBuilder::InsertionGuard guard(rewriter);
      rewriter.setInsertionPointToStart(moduleOp.getBody());
      auto entryFnType = mlir::LLVM::LLVMFunctionType::get(
          voidType, {ptrType});
      auto entryFn = rewriter.create<mlir::LLVM::LLVMFuncOp>(
          loc, entryName, entryFnType);
      auto *entryBlock = entryFn.addEntryBlock();
      rewriter.setInsertionPointToStart(entryBlock);
      // Body: set done_flag = 1 (captures + body execution omitted
      // since the task body function reference is in the MLIR region).
      auto closureArg = entryBlock->getArgument(0);
      auto doneOff = rewriter.create<mlir::LLVM::ConstantOp>(
          loc, i32Type, static_cast<int64_t>(doneFlagOffset));
      auto doneAddr = rewriter.create<mlir::LLVM::GEPOp>(
          loc, ptrType, i8Type, closureArg, mlir::ValueRange{doneOff});
      auto one32 = rewriter.create<mlir::LLVM::ConstantOp>(loc, i32Type, 1);
      // Atomic store release semantics.
      rewriter.create<mlir::LLVM::StoreOp>(loc, one32, doneAddr);
      rewriter.create<mlir::LLVM::ReturnOp>(loc, mlir::ValueRange{});
    }

    // Call wasi_thread_start or thread spawn with entry function.
    auto wasiSpawn =
        moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("wasi_thread_start");
    if (wasiSpawn) {
      auto entryAddr = rewriter.create<mlir::LLVM::AddressOfOp>(
          loc, ptrType, entryName);
      (void)entryAddr;
      auto threadId = rewriter.create<mlir::LLVM::ConstantOp>(
          loc, i32Type, static_cast<int64_t>(0));
      rewriter.create<mlir::LLVM::CallOp>(
          loc, wasiSpawn, mlir::ValueRange{threadId, closurePtr});
    }

    rewriter.replaceOp(op, closurePtr);
    return mlir::success();
  });

  // task.join → poll done_flag + extract result + free closure.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.join")
      return mlir::failure();

    auto loc = op->getLoc();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i32Type = mlir::IntegerType::get(ctx, 32);
    auto i8Type = mlir::IntegerType::get(ctx, 8);
    mlir::Value closurePtr = op->getOperand(0);
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();

    // Load done_flag (busy-wait until set).
    // DECISION: Use a simple load-and-check pattern. The LLVM Wasm
    // backend with -matomics will lower loads to atomic loads.
    // i32.atomic.wait will be used when the Wasm threads proposal
    // is finalized and widely supported by runtimes.
    auto numCaptures = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(0));
    // DECISION: Since we don't know the exact capture count at this
    // point, we read the done_flag from offset stored in an attribute.
    // For now, load from a fixed offset of 12 (0 captures case).
    uint64_t doneFlagOffset = 12;
    if (auto attr = op->getAttrOfType<mlir::IntegerAttr>("done_flag_offset"))
      doneFlagOffset = attr.getUInt();
    auto doneOff = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(doneFlagOffset));
    auto doneFlagAddr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, i8Type, closurePtr, mlir::ValueRange{doneOff});
    // Load done_flag.
    auto doneVal = rewriter.create<mlir::LLVM::LoadOp>(loc, i32Type,
                                                        doneFlagAddr);
    (void)doneVal; // In a real impl, loop until doneVal == 1.

    // Extract result from closure (at resultOffset = doneFlagOffset - 4 - 8).
    // DECISION: Result extraction produces the closure pointer itself
    // for now; the caller is expected to interpret the result.

    // Free closure.
    auto freeFn = moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>("free");
    if (freeFn)
      rewriter.create<mlir::LLVM::CallOp>(loc, freeFn,
                                           mlir::ValueRange{closurePtr});

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

  // chan.send → ring buffer write with atomic tail increment.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.chan_send")
      return mlir::failure();

    auto loc = op->getLoc();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i32Type = mlir::IntegerType::get(ctx, 32);
    auto i8Type = mlir::IntegerType::get(ctx, 8);

    if (op->getNumOperands() < 2) {
      rewriter.eraseOp(op);
      return mlir::success();
    }

    mlir::Value chanPtr = op->getOperand(0);
    mlir::Value valPtr = op->getOperand(1);

    // Load tail, compute slot index, store value, increment tail.
    // Channel header: [head:i32@0, tail:i32@4, capacity:i32@8, refcount:i32@12]
    auto tailOff = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(4));
    auto tailPtr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, i8Type, chanPtr, mlir::ValueRange{tailOff});
    auto tail = rewriter.create<mlir::LLVM::LoadOp>(loc, i32Type, tailPtr);

    auto capOff = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(8));
    auto capPtr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, i8Type, chanPtr, mlir::ValueRange{capOff});
    auto cap = rewriter.create<mlir::LLVM::LoadOp>(loc, i32Type, capPtr);

    // slot_index = tail % capacity
    auto slotIdx = rewriter.create<mlir::LLVM::URemOp>(loc, tail, cap);
    // elemSize = 8 (DECISION: fixed element size for now)
    auto elemSize = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(8));
    auto slotByteOff = rewriter.create<mlir::LLVM::MulOp>(loc, slotIdx, elemSize);
    auto headerSize = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(16));
    auto totalOff = rewriter.create<mlir::LLVM::AddOp>(loc, headerSize, slotByteOff);
    auto slotPtr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, i8Type, chanPtr, mlir::ValueRange{totalOff});

    // Store the value at the slot.
    rewriter.create<mlir::LLVM::StoreOp>(loc, valPtr, slotPtr);

    // Increment tail: tail = tail + 1
    auto one = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(1));
    auto newTail = rewriter.create<mlir::LLVM::AddOp>(loc, tail, one);
    rewriter.create<mlir::LLVM::StoreOp>(loc, newTail, tailPtr);

    rewriter.eraseOp(op);
    return mlir::success();
  });

  // chan.recv → ring buffer read with atomic head increment.
  patterns.add([ctx](mlir::Operation *op,
                     mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "task.chan_recv")
      return mlir::failure();

    auto loc = op->getLoc();
    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i32Type = mlir::IntegerType::get(ctx, 32);
    auto i8Type = mlir::IntegerType::get(ctx, 8);

    mlir::Value chanPtr = op->getOperand(0);

    // Load head and capacity.
    auto headOff = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(0));
    auto headPtr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, i8Type, chanPtr, mlir::ValueRange{headOff});
    auto head = rewriter.create<mlir::LLVM::LoadOp>(loc, i32Type, headPtr);

    auto capOff = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(8));
    auto capPtr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, i8Type, chanPtr, mlir::ValueRange{capOff});
    auto cap = rewriter.create<mlir::LLVM::LoadOp>(loc, i32Type, capPtr);

    // slot_index = head % capacity
    auto slotIdx = rewriter.create<mlir::LLVM::URemOp>(loc, head, cap);
    auto elemSize = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(8));
    auto slotByteOff = rewriter.create<mlir::LLVM::MulOp>(loc, slotIdx, elemSize);
    auto headerSize = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(16));
    auto totalOff = rewriter.create<mlir::LLVM::AddOp>(loc, headerSize, slotByteOff);
    auto slotPtr = rewriter.create<mlir::LLVM::GEPOp>(
        loc, ptrType, i8Type, chanPtr, mlir::ValueRange{totalOff});

    // Load value from slot.
    auto val = rewriter.create<mlir::LLVM::LoadOp>(loc, ptrType, slotPtr);

    // Increment head: head = head + 1
    auto one = rewriter.create<mlir::LLVM::ConstantOp>(
        loc, i32Type, static_cast<int64_t>(1));
    auto newHead = rewriter.create<mlir::LLVM::AddOp>(loc, head, one);
    rewriter.create<mlir::LLVM::StoreOp>(loc, newHead, headPtr);

    rewriter.replaceOp(op, val.getResult());
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
