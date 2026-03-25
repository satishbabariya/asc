// OwnershipLowering — converts own.* and borrow.* ops to LLVM dialect.
//
// This is a dialect conversion pass that lowers ownership dialect operations
// to LLVM IR operations. After this pass, no own.* or borrow.* operations
// remain in the IR.

#include "asc/CodeGen/OwnershipLowering.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// Type converter: own dialect types → LLVM types
//===----------------------------------------------------------------------===//

void OwnershipLoweringPass::configureTypeConverter(
    mlir::TypeConverter &typeConverter) {
  // !own.val<T> → !llvm.ptr (opaque pointer to heap-allocated T)
  typeConverter.addConversion([](mlir::Type type) -> std::optional<mlir::Type> {
    llvm::StringRef typeName = type.getAbstractType().getName();
    if (typeName.contains("own.val")) {
      // Owned values are represented as pointers to heap-allocated memory.
      return mlir::LLVM::LLVMPointerType::get(type.getContext());
    }
    return std::nullopt;
  });

  // !borrow<T> → !llvm.ptr (pointer to the borrowed value)
  typeConverter.addConversion([](mlir::Type type) -> std::optional<mlir::Type> {
    llvm::StringRef typeName = type.getAbstractType().getName();
    if (typeName.contains("borrow")) {
      // Borrows are just pointers at runtime.
      return mlir::LLVM::LLVMPointerType::get(type.getContext());
    }
    return std::nullopt;
  });

  // Standard types pass through unchanged.
  typeConverter.addConversion([](mlir::Type type) -> std::optional<mlir::Type> {
    if (type.isIntOrIndexOrFloat() ||
        type.isa<mlir::LLVM::LLVMPointerType>() ||
        type.isa<mlir::FunctionType>())
      return type;
    return std::nullopt;
  });
}

//===----------------------------------------------------------------------===//
// Conversion target
//===----------------------------------------------------------------------===//

void OwnershipLoweringPass::configureTarget(mlir::ConversionTarget &target) {
  // LLVM dialect is legal (we lower TO it).
  target.addLegalDialect<mlir::LLVM::LLVMDialect>();

  // func dialect operations are legal (they'll be lowered in a separate pass).
  target.addLegalDialect<mlir::func::FuncDialect>();

  // All own.* operations are illegal (must be lowered).
  target.addIllegalOp<mlir::Operation>();

  // Mark own.* and borrow.* operations as illegal by name prefix.
  target.addDynamicallyLegalOp<mlir::Operation>([](mlir::Operation *op) {
    llvm::StringRef name = op->getName().getStringRef();
    return !name.starts_with("own.") && !name.starts_with("borrow.");
  });
}

//===----------------------------------------------------------------------===//
// Lowering patterns
//===----------------------------------------------------------------------===//

/// Helper: get or declare the malloc function in the module.
static mlir::LLVM::LLVMFuncOp
getOrInsertMalloc(mlir::ModuleOp module, mlir::OpBuilder &builder) {
  auto mallocFunc = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
  if (mallocFunc)
    return mallocFunc;

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());

  auto i64Type = mlir::IntegerType::get(module.getContext(), 64);
  auto ptrType = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto mallocType = mlir::LLVM::LLVMFunctionType::get(ptrType, {i64Type});

  return builder.create<mlir::LLVM::LLVMFuncOp>(module.getLoc(), "malloc",
                                                  mallocType);
}

/// Helper: get or declare the free function in the module.
static mlir::LLVM::LLVMFuncOp
getOrInsertFree(mlir::ModuleOp module, mlir::OpBuilder &builder) {
  auto freeFunc = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("free");
  if (freeFunc)
    return freeFunc;

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());

  auto voidType = mlir::LLVM::LLVMVoidType::get(module.getContext());
  auto ptrType = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto freeType = mlir::LLVM::LLVMFunctionType::get(voidType, {ptrType});

  return builder.create<mlir::LLVM::LLVMFuncOp>(module.getLoc(), "free",
                                                  freeType);
}

/// Helper: get or declare memcpy.
static mlir::LLVM::LLVMFuncOp
getOrInsertMemcpy(mlir::ModuleOp module, mlir::OpBuilder &builder) {
  auto memcpyFunc = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("memcpy");
  if (memcpyFunc)
    return memcpyFunc;

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());

  auto ptrType = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto i64Type = mlir::IntegerType::get(module.getContext(), 64);
  auto memcpyType =
      mlir::LLVM::LLVMFunctionType::get(ptrType, {ptrType, ptrType, i64Type});

  return builder.create<mlir::LLVM::LLVMFuncOp>(module.getLoc(), "memcpy",
                                                  memcpyType);
}

void OwnershipLoweringPass::populatePatterns(
    mlir::RewritePatternSet &patterns,
    mlir::TypeConverter &typeConverter) {
  mlir::MLIRContext *ctx = patterns.getContext();

  // Pattern: own.alloc → malloc + bitcast
  patterns.add([&](mlir::Operation *op,
                   mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "own.alloc")
      return mlir::failure();

    mlir::Location loc = op->getLoc();
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    auto mallocFn = getOrInsertMalloc(moduleOp, rewriter);

    // Get the size from the operation's attribute.
    uint64_t size = 8; // Default size; real impl reads from type metadata.
    if (auto sizeAttr = op->getAttrOfType<mlir::IntegerAttr>("size"))
      size = sizeAttr.getUInt();

    auto i64Type = mlir::IntegerType::get(ctx, 64);
    auto sizeConst =
        rewriter.create<mlir::LLVM::ConstantOp>(loc, i64Type, size);

    auto callOp = rewriter.create<mlir::LLVM::CallOp>(
        loc, mallocFn, mlir::ValueRange{sizeConst});

    rewriter.replaceOp(op, callOp.getResults());
    return mlir::success();
  });

  // Pattern: own.drop → call destructor (if any) + free
  patterns.add([&](mlir::Operation *op,
                   mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "own.drop")
      return mlir::failure();

    mlir::Location loc = op->getLoc();
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    auto freeFn = getOrInsertFree(moduleOp, rewriter);

    mlir::Value ptr = op->getOperand(0);

    // Check if the type has a custom destructor (__drop function).
    if (auto dropName = op->getAttrOfType<mlir::StringAttr>("drop_fn")) {
      auto dropFn =
          moduleOp.lookupSymbol<mlir::LLVM::LLVMFuncOp>(dropName.getValue());
      if (dropFn) {
        rewriter.create<mlir::LLVM::CallOp>(loc, dropFn,
                                             mlir::ValueRange{ptr});
      }
    }

    // Free the memory.
    rewriter.create<mlir::LLVM::CallOp>(loc, freeFn, mlir::ValueRange{ptr});
    rewriter.eraseOp(op);
    return mlir::success();
  });

  // Pattern: own.move → identity (just forward the pointer)
  patterns.add([&](mlir::Operation *op,
                   mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "own.move")
      return mlir::failure();

    // Move is a no-op at runtime — the SSA value is just forwarded.
    // The source is invalidated at the type level (already checked by
    // MoveCheck), so we simply replace the result with the operand.
    rewriter.replaceOp(op, op->getOperands());
    return mlir::success();
  });

  // Pattern: own.borrow / own.borrow_mut → identity (pointer pass-through)
  patterns.add([&](mlir::Operation *op,
                   mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    llvm::StringRef name = op->getName().getStringRef();
    if (name != "own.borrow" && name != "own.borrow_mut" &&
        name != "own.borrow_ref")
      return mlir::failure();

    // Borrows are compile-time-only concepts. At runtime, the borrow
    // value IS the pointer to the owned value. No-op.
    rewriter.replaceOp(op, op->getOperands());
    return mlir::success();
  });

  // Pattern: own.copy → malloc + memcpy (deep copy)
  patterns.add([&](mlir::Operation *op,
                   mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "own.copy")
      return mlir::failure();

    mlir::Location loc = op->getLoc();
    auto moduleOp = op->getParentOfType<mlir::ModuleOp>();
    auto mallocFn = getOrInsertMalloc(moduleOp, rewriter);
    auto memcpyFn = getOrInsertMemcpy(moduleOp, rewriter);

    mlir::Value srcPtr = op->getOperand(0);

    // Get size from attribute or default.
    uint64_t size = 8;
    if (auto sizeAttr = op->getAttrOfType<mlir::IntegerAttr>("size"))
      size = sizeAttr.getUInt();

    auto i64Type = mlir::IntegerType::get(ctx, 64);
    auto sizeConst =
        rewriter.create<mlir::LLVM::ConstantOp>(loc, i64Type, size);

    // Allocate new memory.
    auto allocCall = rewriter.create<mlir::LLVM::CallOp>(
        loc, mallocFn, mlir::ValueRange{sizeConst});
    mlir::Value dstPtr = allocCall.getResult();

    // Copy the data.
    rewriter.create<mlir::LLVM::CallOp>(
        loc, memcpyFn, mlir::ValueRange{dstPtr, srcPtr, sizeConst});

    rewriter.replaceOp(op, dstPtr);
    return mlir::success();
  });

  // Pattern: own.try_scope → inline region (EH lowering deferred to backend)
  patterns.add([&](mlir::Operation *op,
                   mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    if (op->getName().getStringRef() != "own.try_scope")
      return mlir::failure();

    // Inline the body region at the current point.
    if (op->getNumRegions() > 0 && !op->getRegion(0).empty()) {
      rewriter.inlineRegionBefore(op->getRegion(0), op->getBlock(),
                                   std::next(mlir::Block::iterator(op)));
    }
    rewriter.eraseOp(op);
    return mlir::success();
  });

  // Pattern: own.catch_scope / own.cleanup_scope → landingpad / catch block
  patterns.add([&](mlir::Operation *op,
                   mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    llvm::StringRef name = op->getName().getStringRef();
    if (name != "own.catch_scope" && name != "own.cleanup_scope")
      return mlir::failure();

    // The cleanup region is inlined as a separate block that will be
    // connected to the EH infrastructure during LLVM lowering.
    if (op->getNumRegions() > 0 && !op->getRegion(0).empty()) {
      rewriter.inlineRegionBefore(op->getRegion(0), op->getBlock(),
                                   std::next(mlir::Block::iterator(op)));
    }
    rewriter.eraseOp(op);
    return mlir::success();
  });

  // Pattern: own.rethrow → llvm.resume / wasm rethrow
  patterns.add([&](mlir::Operation *op,
                   mlir::PatternRewriter &rewriter) -> mlir::LogicalResult {
    llvm::StringRef name = op->getName().getStringRef();
    if (name != "own.rethrow" && name != "own.resume")
      return mlir::failure();

    mlir::Location loc = op->getLoc();
    // Lower to llvm.unreachable as a placeholder — the real EH lowering
    // happens during the LLVM backend pass.
    rewriter.create<mlir::LLVM::UnreachableOp>(loc);
    rewriter.eraseOp(op);
    return mlir::success();
  });
}

//===----------------------------------------------------------------------===//
// Pass entry point
//===----------------------------------------------------------------------===//

void OwnershipLoweringPass::runOnOperation() {
  mlir::ModuleOp module = getOperation();
  mlir::MLIRContext *ctx = &getContext();

  // Set up type converter.
  mlir::TypeConverter typeConverter;
  configureTypeConverter(typeConverter);

  // Set up conversion target.
  mlir::ConversionTarget target(*ctx);
  configureTarget(target);

  // Populate rewrite patterns.
  mlir::RewritePatternSet patterns(ctx);
  populatePatterns(patterns, typeConverter);

  // Apply the conversion.
  if (mlir::failed(
          mlir::applyPartialConversion(module, target, std::move(patterns)))) {
    signalPassFailure();
  }
}

std::unique_ptr<mlir::Pass> createOwnershipLoweringPass() {
  return std::make_unique<OwnershipLoweringPass>();
}

} // namespace asc
