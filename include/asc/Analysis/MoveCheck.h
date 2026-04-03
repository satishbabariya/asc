#ifndef ASC_ANALYSIS_MOVECHECK_H
#define ASC_ANALYSIS_MOVECHECK_H

#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>

namespace asc {

/// Tracks the state of an owned value through the program.
enum class MoveState {
  Live,       // Value is owned and available
  Moved,      // Value has been moved (consumed)
  MaybeMoved, // Value may have been moved on some but not all paths
  Dropped,    // Value has been explicitly dropped
};

/// Move checking pass — verifies linearity of own.val values.
///
/// Ensures that every value of type !own.val<T> is:
/// 1. Used (moved) exactly once on every execution path, OR
/// 2. Explicitly dropped.
///
/// Detects the following errors:
/// - Use after move: accessing a value that was already consumed.
/// - Double move: moving a value that was already moved.
/// - Move in one branch: value moved in one branch of a conditional but
///   not the other (partial move).
/// - Unused owned value: value is never consumed (resource leak).
///
/// The analysis is a forward dataflow analysis tracking move state per value.
class MoveCheckPass
    : public mlir::PassWrapper<MoveCheckPass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(MoveCheckPass)

  llvm::StringRef getArgument() const override { return "asc-move-check"; }
  llvm::StringRef getDescription() const override {
    return "Verify linearity of owned values (no use-after-move)";
  }

  void runOnOperation() override;

private:
  /// Per-block state: map from owned value to its move state.
  using ValueStateMap = llvm::DenseMap<mlir::Value, MoveState>;

  /// Analyze a single block, updating the state map.
  void analyzeBlock(mlir::Block &block, ValueStateMap &stateAtEntry);

  /// Merge states from predecessor blocks at a join point.
  ValueStateMap mergeStates(
      const llvm::SmallVector<ValueStateMap *, 4> &predStates);

  /// Check that an operation is not using a moved value.
  void checkOperandStates(mlir::Operation *op, const ValueStateMap &states);

  /// Check for partially moved values in conditional branches.
  void checkPartialMoves(mlir::Operation *branchOp,
                         const ValueStateMap &thenState,
                         const ValueStateMap &elseState);

  /// Check that all owned values are consumed by function exit.
  void checkAllConsumed(mlir::func::FuncOp func,
                        const ValueStateMap &exitState);

  /// Report a use-after-move error.
  void reportUseAfterMove(mlir::Operation *use, mlir::Value value,
                          mlir::Operation *moveOp);

  /// Report a double-move error.
  void reportDoubleMove(mlir::Operation *secondMove, mlir::Value value,
                        mlir::Operation *firstMove);

  /// Track where each value was first moved.
  llvm::DenseMap<mlir::Value, mlir::Operation *> firstMoveOp;
};

/// Create a move check pass.
std::unique_ptr<mlir::Pass> createMoveCheckPass();

} // namespace asc

#endif // ASC_ANALYSIS_MOVECHECK_H
