#ifndef ASC_ANALYSIS_ALIASCHECK_H
#define ASC_ANALYSIS_ALIASCHECK_H

#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>

namespace asc {

/// Describes the kind of access a borrow provides.
enum class BorrowKind {
  Shared,   // &T — immutable, many allowed simultaneously
  Mutable,  // &mut T — exclusive, no other borrows allowed
};

/// Represents an active borrow for alias checking.
struct ActiveBorrow {
  mlir::Value borrowValue;   // The SSA value of the borrow itself
  mlir::Value originValue;   // The owned value being borrowed
  BorrowKind kind;
  mlir::Operation *borrowOp; // The operation that created the borrow
};

/// Alias checking pass — verifies the three borrow rules from RFC-0006:
///
/// Rule A: At any program point, for a given owned value, there may be
///         either one &mut borrow OR any number of & borrows, but not both.
///
/// Rule B: A borrowed reference must not outlive its origin (enforced by
///         region inference, but checked here for diagnostic quality).
///
/// Rule C: An owned value must not be moved or dropped while borrows are
///         active against it.
///
/// This pass consumes the RegionInferenceResult to determine which borrows
/// are simultaneously active at each program point.
class AliasCheckPass
    : public mlir::PassWrapper<AliasCheckPass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AliasCheckPass)

  llvm::StringRef getArgument() const override { return "asc-alias-check"; }
  llvm::StringRef getDescription() const override {
    return "Verify borrow aliasing rules (no conflicting borrows)";
  }

  void runOnOperation() override;

private:
  /// Collect all borrow operations in the function.
  void collectBorrows(mlir::func::FuncOp func);

  /// Check Rule A: exclusive-or-shared at each program point.
  void checkExclusivity(mlir::func::FuncOp func);

  /// Check Rule B: borrow does not outlive origin.
  void checkBorrowLifetime(mlir::func::FuncOp func);

  /// Check Rule C: no move/drop while borrow is active.
  void checkNoMoveWhileBorrowed(mlir::func::FuncOp func);

  /// Report a borrow conflict diagnostic.
  void reportConflict(mlir::Operation *conflicting,
                      const ActiveBorrow &existing,
                      const ActiveBorrow &incoming);

  /// All borrows in the function, grouped by their origin value.
  llvm::DenseMap<mlir::Value, llvm::SmallVector<ActiveBorrow, 4>>
      borrowsByOrigin;
};

/// Create an alias check pass.
std::unique_ptr<mlir::Pass> createAliasCheckPass();

} // namespace asc

#endif // ASC_ANALYSIS_ALIASCHECK_H
