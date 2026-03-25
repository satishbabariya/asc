#ifndef ASC_CODEGEN_OWNERSHIPLOWERING_H
#define ASC_CODEGEN_OWNERSHIPLOWERING_H

#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace asc {

/// Ownership lowering pass — converts own.* and borrow.* ops to LLVM dialect.
///
/// This is a dialect conversion pass that lowers the custom ownership dialect
/// operations to LLVM IR operations. The key transformations are:
///
/// - own.alloc<T>       → llvm.call @malloc(sizeof(T)), llvm.bitcast
/// - own.drop<T>        → llvm.call @T.__drop(ptr), llvm.call @free(ptr)
/// - own.move            → identity (SSA rename, source becomes undef)
/// - own.borrow          → identity (pointer pass-through, compile-time only)
/// - own.borrow_mut      → identity (pointer pass-through, compile-time only)
/// - own.copy            → llvm.call @memcpy + new alloc for deep copy
/// - own.try_scope       → region inlining with EH setup
/// - own.catch_scope     → landingpad / Wasm catch
/// - own.cleanup_scope   → cleanup block with drops
///
/// After this pass, no own.* or borrow.* operations remain in the IR.
class OwnershipLoweringPass
    : public mlir::PassWrapper<OwnershipLoweringPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OwnershipLoweringPass)

  llvm::StringRef getArgument() const override {
    return "asc-ownership-lowering";
  }
  llvm::StringRef getDescription() const override {
    return "Lower own.* and borrow.* dialect ops to LLVM dialect";
  }

  void runOnOperation() override;

private:
  /// Set up the conversion target (what's legal after lowering).
  void configureTarget(mlir::ConversionTarget &target);

  /// Populate the rewrite pattern set with all ownership lowering patterns.
  void populatePatterns(mlir::RewritePatternSet &patterns,
                        mlir::TypeConverter &typeConverter);

  /// Build the type converter for own dialect types → LLVM types.
  void configureTypeConverter(mlir::TypeConverter &typeConverter);
};

//===----------------------------------------------------------------------===//
// Individual lowering patterns
//===----------------------------------------------------------------------===//

/// Convert own.alloc to malloc call + bitcast.
struct OwnAllocLowering : public mlir::OpConversionPattern<mlir::Operation> {
  using mlir::OpConversionPattern<mlir::Operation>::OpConversionPattern;
  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op, mlir::ArrayRef<mlir::Value> operands,
                  mlir::ConversionPatternRewriter &rewriter) const override;
};

/// Convert own.drop to destructor call + free.
struct OwnDropLowering : public mlir::OpConversionPattern<mlir::Operation> {
  using mlir::OpConversionPattern<mlir::Operation>::OpConversionPattern;
  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op, mlir::ArrayRef<mlir::Value> operands,
                  mlir::ConversionPatternRewriter &rewriter) const override;
};

/// Convert own.move to SSA identity (the value is just forwarded).
struct OwnMoveLowering : public mlir::OpConversionPattern<mlir::Operation> {
  using mlir::OpConversionPattern<mlir::Operation>::OpConversionPattern;
  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op, mlir::ArrayRef<mlir::Value> operands,
                  mlir::ConversionPatternRewriter &rewriter) const override;
};

/// Convert own.borrow / own.borrow_mut to pass-through (no-op at runtime).
struct BorrowLowering : public mlir::OpConversionPattern<mlir::Operation> {
  using mlir::OpConversionPattern<mlir::Operation>::OpConversionPattern;
  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op, mlir::ArrayRef<mlir::Value> operands,
                  mlir::ConversionPatternRewriter &rewriter) const override;
};

/// Convert own.copy to alloc + memcpy for deep copy.
struct OwnCopyLowering : public mlir::OpConversionPattern<mlir::Operation> {
  using mlir::OpConversionPattern<mlir::Operation>::OpConversionPattern;
  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op, mlir::ArrayRef<mlir::Value> operands,
                  mlir::ConversionPatternRewriter &rewriter) const override;
};

/// Create the ownership lowering pass.
std::unique_ptr<mlir::Pass> createOwnershipLoweringPass();

} // namespace asc

#endif // ASC_CODEGEN_OWNERSHIPLOWERING_H
