#ifndef ASC_ANALYSIS_LINEARITYCHECK_H
#define ASC_ANALYSIS_LINEARITYCHECK_H

#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>

namespace asc {

/// Linearity checking pass -- ensures every !own.val<T> SSA value has
/// exactly one consuming use on every control-flow path.
///
/// Complements MoveCheck by providing specific error codes:
/// - E005 "value never consumed" -- 0 consuming uses and 0 non-consuming
///   uses (completely unused owned value, guaranteed resource leak).
/// - E006 "value consumed multiple times" -- 2+ consuming uses in the
///   same basic block (double-free risk).
///
/// @copy types are exempt because they are not wrapped in !own.val<T>
/// at the MLIR level.
///
/// Values that have non-consuming uses (borrows, field accesses) but no
/// explicit consuming use are NOT flagged -- DropInsertion (which runs
/// after this pass) will insert the necessary drops.
class LinearityCheckPass
    : public mlir::PassWrapper<LinearityCheckPass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LinearityCheckPass)

  llvm::StringRef getArgument() const override {
    return "asc-linearity-check";
  }
  llvm::StringRef getDescription() const override {
    return "Verify linearity of owned values (E005/E006)";
  }

  void runOnOperation() override;

private:
  /// Check if an operation consumes its operand (move semantics).
  static bool isConsumingOp(mlir::Operation *op);

  /// Count consuming uses of a value within a single block.
  /// Returns a map from block to the list of consuming operations.
  using ConsumeMap =
      llvm::DenseMap<mlir::Block *, llvm::SmallVector<mlir::Operation *, 2>>;
  ConsumeMap getConsumingUses(mlir::Value value);

  /// Check if a value has any non-consuming uses.
  static bool hasAnyUse(mlir::Value value);

  /// Report E005: value never consumed.
  void reportNeverConsumed(mlir::Operation *defOp, mlir::Value value);

  /// Report E006: value consumed multiple times in one block.
  void reportDoubleConsume(mlir::Value value,
                           llvm::ArrayRef<mlir::Operation *> consumers);
};

/// Create a linearity check pass.
std::unique_ptr<mlir::Pass> createLinearityCheckPass();

} // namespace asc

#endif // ASC_ANALYSIS_LINEARITYCHECK_H
