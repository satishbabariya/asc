#ifndef ASC_ANALYSIS_PANICSCOPEWRAP_H
#define ASC_ANALYSIS_PANICSCOPEWRAP_H

#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>

namespace asc {

/// Panic scope wrapping pass — wraps scopes in try/catch for Wasm EH.
///
/// When a panic can occur inside a scope that holds owned resources, the
/// resources must still be dropped to prevent leaks. This pass wraps such
/// scopes in try/catch blocks that run destructors on panic.
///
/// This implements the panic safety model from RFC-0009:
/// 1. Identify scopes that contain potentially-panicking operations
///    (array bounds checks, integer overflow, explicit panic!(), unwrap()).
/// 2. Identify owned values that are live across those operations.
/// 3. Wrap the scope in own.try_scope / own.catch_scope / own.cleanup_scope
///    operations that encode the cleanup sequence.
/// 4. In the cleanup (catch/landing-pad) path, insert drops for all owned
///    values that were live at the panic point.
///
/// On Wasm targets, this generates Wasm EH (try/catch/throw) instructions.
/// On native targets, this generates LLVM landingpad-based EH.
///
/// Scopes that provably cannot panic are left unwrapped for efficiency.
class PanicScopeWrapPass
    : public mlir::PassWrapper<PanicScopeWrapPass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PanicScopeWrapPass)

  llvm::StringRef getArgument() const override {
    return "asc-panic-scope-wrap";
  }
  llvm::StringRef getDescription() const override {
    return "Wrap scopes containing panicking ops in try/catch for cleanup";
  }

  void runOnOperation() override;

private:
  /// A scope that may need wrapping.
  struct ScopeInfo {
    mlir::Operation *scopeOp;  // The region-holding operation
    llvm::SmallVector<mlir::Operation *, 4> panicPoints;
    llvm::SmallVector<mlir::Value, 8> liveOwnedValues;
    bool needsWrap = false;
  };

  /// Identify all scopes and their panic points.
  void identifyScopes(mlir::func::FuncOp func);

  /// Determine which owned values are live across panic points.
  void computeLiveAcrossPanic(ScopeInfo &scope);

  /// Wrap a scope with try/catch cleanup operations.
  void wrapScope(ScopeInfo &scope);

  /// Build the cleanup block that drops all live owned values.
  void buildCleanupBlock(mlir::OpBuilder &builder,
                         const llvm::SmallVector<mlir::Value, 8> &values);

  /// Check if an operation can potentially panic.
  bool canPanic(mlir::Operation *op) const;

  /// Check if a scope provably never panics.
  bool isNoPanicScope(mlir::Operation *scopeOp) const;

  /// All scopes that need analysis.
  llvm::SmallVector<ScopeInfo, 8> scopes;

  /// Whether we are targeting Wasm (uses Wasm EH) or native (landingpad EH).
  bool isWasmTarget = true;
};

/// Create a panic scope wrap pass.
std::unique_ptr<mlir::Pass> createPanicScopeWrapPass();

} // namespace asc

#endif // ASC_ANALYSIS_PANICSCOPEWRAP_H
