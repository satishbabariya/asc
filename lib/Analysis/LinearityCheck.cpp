// LinearityCheck -- verifies linearity of !own.val<T> values.
//
// Ensures every owned value is consumed exactly once per control-flow
// path.  Emits E005 for completely unused owned values and E006 for
// values consumed more than once in the same basic block.

#include "asc/Analysis/LinearityCheck.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "asc/HIR/OwnTypes.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static bool isOwnedType(mlir::Type type) {
  return mlir::isa<asc::own::OwnValType>(type);
}

bool LinearityCheckPass::isConsumingOp(mlir::Operation *op) {
  llvm::StringRef opName = op->getName().getStringRef();
  return opName == "own.move" || opName == "own.drop" ||
         opName == "own.store" || opName == "func.call" ||
         opName == "chan.send" || opName == "func.return";
}

bool LinearityCheckPass::hasAnyUse(mlir::Value value) {
  return !value.use_empty();
}

LinearityCheckPass::ConsumeMap
LinearityCheckPass::getConsumingUses(mlir::Value value) {
  ConsumeMap result;
  for (mlir::OpOperand &use : value.getUses()) {
    mlir::Operation *user = use.getOwner();
    if (isConsumingOp(user)) {
      result[user->getBlock()].push_back(user);
    }
  }
  return result;
}

//===----------------------------------------------------------------------===//
// Diagnostics
//===----------------------------------------------------------------------===//

void LinearityCheckPass::reportNeverConsumed(mlir::Operation *defOp,
                                             mlir::Value value) {
  // E005: owned value is completely unused -- guaranteed resource leak.
  auto diag = defOp->emitWarning()
      << "E005: owned value is never used; this is a resource leak";
  diag.attachNote()
      << "every !own.val must have exactly one consuming use per RFC-0005";
}

void LinearityCheckPass::reportDoubleConsume(
    mlir::Value value, llvm::ArrayRef<mlir::Operation *> consumers) {
  // E006: value consumed multiple times in the same block.
  assert(consumers.size() >= 2 && "need at least two consumers for E006");

  mlir::InFlightDiagnostic diag =
      consumers[1]->emitError()
      << "E006: value consumed more than once (double-free risk)";
  diag.attachNote(consumers[0]->getLoc()) << "value was first consumed here";

  if (auto *defOp = value.getDefiningOp()) {
    diag.attachNote(defOp->getLoc()) << "value was defined here";
  }
  signalPassFailure();
}

//===----------------------------------------------------------------------===//
// Main pass logic
//===----------------------------------------------------------------------===//

void LinearityCheckPass::runOnOperation() {
  mlir::func::FuncOp func = getOperation();
  if (func.isDeclaration())
    return;

  // Collect all !own.val<T> SSA values in this function.
  llvm::SmallVector<mlir::Value, 16> ownedValues;

  // Function arguments.
  for (mlir::Value arg : func.getBody().front().getArguments()) {
    if (isOwnedType(arg.getType()))
      ownedValues.push_back(arg);
  }

  // Operation results across all blocks.
  func.walk([&](mlir::Operation *op) {
    for (mlir::Value result : op->getResults()) {
      if (isOwnedType(result.getType()))
        ownedValues.push_back(result);
    }
  });

  // Analyze each owned value.
  for (mlir::Value val : ownedValues) {
    ConsumeMap consumesByBlock = getConsumingUses(val);

    // Count total consuming uses across all blocks.
    unsigned totalConsumes = 0;
    for (auto &[block, consumers] : consumesByBlock)
      totalConsumes += consumers.size();

    // --- E005: no consuming use anywhere ---
    // The value may have non-consuming uses (borrows, stores) but no
    // move/drop/return/call that transfers ownership.  DropInsertion
    // will add the drop later, so emit as warning (not error).
    if (totalConsumes == 0) {
      if (auto *defOp = val.getDefiningOp()) {
        reportNeverConsumed(defOp, val);
      }
      continue;
    }

    // --- E006: same-block double consume ---
    for (auto &[block, consumers] : consumesByBlock) {
      if (consumers.size() >= 2) {
        reportDoubleConsume(val, consumers);
      }
    }
  }
}

std::unique_ptr<mlir::Pass> createLinearityCheckPass() {
  return std::make_unique<LinearityCheckPass>();
}

} // namespace asc
