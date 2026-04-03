#ifndef ASC_ANALYSIS_DROPINSERTION_H
#define ASC_ANALYSIS_DROPINSERTION_H

#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>

namespace asc {

/// Drop insertion pass — inserts own.drop operations at scope exits.
///
/// After liveness analysis and move checking, this transform pass inserts
/// explicit drop operations for owned values that go out of scope without
/// being moved. This is similar to Rust's implicit drop semantics.
///
/// Algorithm:
/// 1. For each block, identify owned values that are live-in but not live-out
///    and not moved within the block — these need drops at block exit.
/// 2. For function returns, insert drops for all live owned values that are
///    not being returned.
/// 3. For early exits (break, continue, return in nested scope), insert drops
///    for values that would be skipped.
/// 4. Drops are inserted in reverse declaration order (LIFO) to match
///    Rust's drop ordering guarantee.
///
/// This is a transform pass (modifies the IR) rather than an analysis pass.
class DropInsertionPass
    : public mlir::PassWrapper<DropInsertionPass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(DropInsertionPass)

  llvm::StringRef getArgument() const override {
    return "asc-drop-insertion";
  }
  llvm::StringRef getDescription() const override {
    return "Insert own.drop operations at scope exits for owned values";
  }

  void runOnOperation() override;

private:
  /// Information about an owned value that may need dropping.
  struct OwnedValueInfo {
    mlir::Value value;
    mlir::Operation *defOp;  // Where the value was defined
    unsigned declOrder;       // Declaration order for LIFO dropping
  };

  /// Collect all owned values in the function.
  void collectOwnedValues(mlir::func::FuncOp func);

  /// Insert drops at the end of blocks where values go out of scope.
  void insertBlockExitDrops(mlir::Block &block);

  /// Insert drops before return operations for non-returned owned values.
  void insertReturnDrops(mlir::func::FuncOp func);

  /// Insert drops before early exits (break, continue) in loops.
  void insertEarlyExitDrops(mlir::func::FuncOp func);

  /// Insert a single own.drop operation before the given operation.
  void insertDropBefore(mlir::Operation *insertPoint, mlir::Value value);

  /// Check if a value's type has a custom Drop implementation.
  bool hasCustomDrop(mlir::Value value) const;

  /// Check if a value has already been moved/consumed at the given point.
  bool isConsumedBefore(mlir::Value value, mlir::Operation *point) const;

  /// All owned values in the function, indexed by defining block.
  llvm::DenseMap<mlir::Block *, llvm::SmallVector<OwnedValueInfo, 8>>
      ownedValuesByBlock;

  /// Counter for declaration ordering.
  unsigned nextDeclOrder = 0;
};

/// Create a drop insertion pass.
std::unique_ptr<mlir::Pass> createDropInsertionPass();

} // namespace asc

#endif // ASC_ANALYSIS_DROPINSERTION_H
