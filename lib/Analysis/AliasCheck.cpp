// AliasCheck — verifies borrow aliasing rules (RFC-0006 Rules A, B, C).
//
// Rule A: At any point, either one &mut OR any number of &, not both.
// Rule B: Borrows must not outlive their origin.
// Rule C: No move/drop while borrows are active.

#include "asc/Analysis/AliasCheck.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

/// Trace an SSA value through llvm.load ops to find the root alloca/variable.
static mlir::Value traceToRoot(mlir::Value val) {
  for (int depth = 0; depth < 16; ++depth) {
    if (auto *defOp = val.getDefiningOp()) {
      if (defOp->getName().getStringRef() == "llvm.load" &&
          defOp->getNumOperands() > 0) {
        val = defOp->getOperand(0);
        continue;
      }
    }
    break;
  }
  return val;
}

//===----------------------------------------------------------------------===//
// Helper: check whether two borrows have overlapping live ranges.
//===----------------------------------------------------------------------===//

/// Check whether two borrows have overlapping live ranges.
/// This is a simplified overlap check; a full implementation would use
/// the RegionInferenceResult to compare region point sets.
static bool borrowsOverlap(const ActiveBorrow &a, const ActiveBorrow &b) {
  // If both borrows are in the same block, check statement ordering.
  mlir::Block *blockA = a.borrowOp->getBlock();
  mlir::Block *blockB = b.borrowOp->getBlock();

  if (blockA != blockB) {
    // Cross-block borrows: check if either borrow's value has uses
    // in or after the other borrow's block.
    mlir::Value valA = a.borrowValue;
    mlir::Value valB = b.borrowValue;

    // Check if borrow A is used after borrow B's definition.
    bool aUsedAfterB = false;
    for (mlir::OpOperand &use : valA.getUses()) {
      mlir::Operation *useOp = use.getOwner();
      if (useOp->getBlock() == blockB && b.borrowOp->isBeforeInBlock(useOp)) {
        aUsedAfterB = true;
        break;
      }
    }

    // Check if borrow B is used after borrow A's definition.
    bool bUsedAfterA = false;
    for (mlir::OpOperand &use : valB.getUses()) {
      mlir::Operation *useOp = use.getOwner();
      if (useOp->getBlock() == blockA && a.borrowOp->isBeforeInBlock(useOp)) {
        bUsedAfterA = true;
        break;
      }
    }

    return aUsedAfterB || bUsedAfterA;
  }

  // Same block: check if the first borrow's value is still used after
  // the second borrow is created.
  mlir::Operation *firstOp = a.borrowOp;
  mlir::Operation *secondOp = b.borrowOp;

  // Determine which comes first.
  const ActiveBorrow *firstBorrow = &a;
  if (!firstOp->isBeforeInBlock(secondOp)) {
    std::swap(firstOp, secondOp);
    firstBorrow = &b;
  }

  // The first borrow is a potential conflict if its value is used after
  // the second borrow point.
  mlir::Value firstVal = firstBorrow->borrowValue;

  for (mlir::OpOperand &use : firstVal.getUses()) {
    mlir::Operation *useOp = use.getOwner();
    if (useOp->getBlock() == blockA && secondOp->isBeforeInBlock(useOp)) {
      return true; // First borrow is still alive when second is created.
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// AliasCheckPass
//===----------------------------------------------------------------------===//

void AliasCheckPass::collectBorrows(mlir::func::FuncOp func) {
  borrowsByOrigin.clear();

  func.walk([&](mlir::Operation *op) {
    llvm::StringRef opName = op->getName().getStringRef();

    bool isSharedBorrow = (opName == "own.borrow_ref");
    bool isMutBorrow = (opName == "own.borrow_mut");
    if (!isSharedBorrow && !isMutBorrow)
      return;

    if (op->getNumResults() == 0 || op->getNumOperands() == 0)
      return;

    ActiveBorrow borrow;
    borrow.borrowValue = op->getResult(0);
    borrow.kind = isMutBorrow ? BorrowKind::Mutable : BorrowKind::Shared;
    borrow.borrowOp = op;

    // Trace the origin through loads to find the root variable (alloca).
    // This ensures borrows of the same variable are grouped together even
    // when the variable is accessed through load→alloca chains.
    borrow.originValue = traceToRoot(op->getOperand(0));

    borrowsByOrigin[borrow.originValue].push_back(borrow);
  });
}

void AliasCheckPass::checkExclusivity(mlir::func::FuncOp func) {
  // For each owned value, check that its borrows don't violate Rule A.
  // We iterate program points and track active borrows.
  for (auto &[origin, borrows] : borrowsByOrigin) {
    if (borrows.size() <= 1)
      continue; // Single borrow can't conflict.

    // Check all pairs of borrows for conflicts.
    for (size_t i = 0; i < borrows.size(); ++i) {
      for (size_t j = i + 1; j < borrows.size(); ++j) {
        const auto &a = borrows[i];
        const auto &b = borrows[j];

        // Rule A: If either borrow is mutable, they conflict unless
        // they are in non-overlapping regions.
        if (a.kind == BorrowKind::Mutable ||
            b.kind == BorrowKind::Mutable) {
          // Check if the borrows' live ranges overlap.
          if (borrowsOverlap(a, b)) {
            reportConflict(b.borrowOp, a, b);
          }
        }
      }
    }
  }
}

void AliasCheckPass::checkBorrowLifetime(mlir::func::FuncOp func) {
  // Rule B: A borrow must not outlive its origin.
  // Check that the origin value is not dropped/moved before all uses
  // of the borrow are complete.
  for (auto &[origin, borrows] : borrowsByOrigin) {
    for (const auto &borrow : borrows) {
      // Find the last use of the borrow value.
      mlir::Operation *lastBorrowUse = nullptr;
      for (mlir::OpOperand &use : borrow.borrowValue.getUses()) {
        mlir::Operation *useOp = use.getOwner();
        if (!lastBorrowUse ||
            (useOp->getBlock() == lastBorrowUse->getBlock() &&
             lastBorrowUse->isBeforeInBlock(useOp))) {
          lastBorrowUse = useOp;
        }
      }

      if (!lastBorrowUse)
        continue;

      // Walk all ops looking for drops/moves of the same origin.
      func.walk([&](mlir::Operation *op) {
        llvm::StringRef opName = op->getName().getStringRef();
        if (opName != "own.drop" && opName != "own.move")
          return;
        if (op->getNumOperands() == 0)
          return;

        mlir::Value droppedRoot = traceToRoot(op->getOperand(0));
        if (droppedRoot != origin)
          return;

        // Same block: drop before last use.
        bool violation = false;
        if (op->getBlock() == lastBorrowUse->getBlock() &&
            op->isBeforeInBlock(lastBorrowUse)) {
          violation = true;
        }
        // Cross-block: drop in a different block than a borrow use.
        // If the borrow has uses in blocks other than the drop block,
        // and the drop exists, flag it conservatively.
        if (!violation && op->getBlock() != lastBorrowUse->getBlock()) {
          for (mlir::OpOperand &use : borrow.borrowValue.getUses()) {
            if (use.getOwner()->getBlock() != op->getBlock()) {
              violation = true;
              break;
            }
          }
        }
        if (violation) {
          mlir::InFlightDiagnostic diag = op->emitError()
              << "[E002] owned value dropped/moved while borrow is still active";
          diag.attachNote(borrow.borrowOp->getLoc())
              << "borrow created here";
          diag.attachNote(lastBorrowUse->getLoc())
              << "borrow used here after drop/move";
          signalPassFailure();
        }
      });
    }
  }
}

void AliasCheckPass::checkNoMoveWhileBorrowed(mlir::func::FuncOp func) {
  // Rule C: No move or drop of an owned value while borrows are active.
  // This overlaps with Rule B but provides better diagnostics for moves
  // specifically.
  func.walk([&](mlir::Operation *op) {
    llvm::StringRef opName = op->getName().getStringRef();
    if (opName != "own.move" && opName != "own.drop")
      return;

    if (op->getNumOperands() == 0)
      return;

    mlir::Value movedRoot = traceToRoot(op->getOperand(0));
    auto it = borrowsByOrigin.find(movedRoot);
    if (it == borrowsByOrigin.end())
      return;

    for (const auto &borrow : it->second) {
      for (mlir::OpOperand &use : borrow.borrowValue.getUses()) {
        mlir::Operation *useOp = use.getOwner();
        bool conflict = false;
        // Same block: move/drop before borrow use.
        if (useOp->getBlock() == op->getBlock() &&
            op->isBeforeInBlock(useOp)) {
          conflict = true;
        }
        // Cross-block: borrow used in a different block than the move/drop.
        if (!conflict && useOp->getBlock() != op->getBlock()) {
          conflict = true;
        }
        if (conflict) {
          llvm::StringRef verb = (opName == "own.move") ? "move" : "drop";
          auto diag = op->emitError() << "[E003] cannot " << verb
                                      << " value while it is borrowed";
          diag.attachNote(borrow.borrowOp->getLoc()) << "borrow created here";
          diag.attachNote(useOp->getLoc()) << "borrowed value used here";
          signalPassFailure();
          return;
        }
      }
    }
  });
}

void AliasCheckPass::reportConflict(mlir::Operation *conflicting,
                                     const ActiveBorrow &existing,
                                     const ActiveBorrow &incoming) {
  llvm::StringRef existingKind =
      existing.kind == BorrowKind::Mutable ? "mutable" : "shared";
  llvm::StringRef incomingKind =
      incoming.kind == BorrowKind::Mutable ? "mutable" : "shared";

  // E001: conflicting borrows
  mlir::InFlightDiagnostic diag = conflicting->emitError()
      << "[E001] cannot create " << incomingKind << " borrow; value already has "
      << "an active " << existingKind << " borrow";
  diag.attachNote(existing.borrowOp->getLoc())
      << "existing " << existingKind << " borrow created here";
  signalPassFailure();
}

void AliasCheckPass::runOnOperation() {
  mlir::func::FuncOp func = getOperation();
  if (func.isDeclaration())
    return;

  // Step 1: Collect all borrows, grouped by origin.
  collectBorrows(func);

  // Step 2: Check exclusivity (Rule A) — E001 diagnostics.
  checkExclusivity(func);

  // Step 3: Check borrow lifetimes (Rule B) — E002 diagnostics.
  checkBorrowLifetime(func);

  // Step 4: Check no move while borrowed (Rule C) — E003 diagnostics.
  checkNoMoveWhileBorrowed(func);
}

std::unique_ptr<mlir::Pass> createAliasCheckPass() {
  return std::make_unique<AliasCheckPass>();
}

} // namespace asc
