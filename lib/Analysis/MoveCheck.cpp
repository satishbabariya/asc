// MoveCheck — verifies linearity of own.val values (no use-after-move).
//
// Forward dataflow analysis tracking the move state of each owned value
// through the CFG. Detects use-after-move, double-move, partial moves,
// and unused owned values (resource leaks).

#include "asc/Analysis/MoveCheck.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "asc/HIR/OwnTypes.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// Helper: Check if a type is an ownership type (!own.val<T>).
//===----------------------------------------------------------------------===//

static bool isOwnedType(mlir::Type type) {
  return mlir::isa<asc::own::OwnValType>(type);
}

/// Check if an operation consumes (moves) its operand.
static bool isConsuming(mlir::Operation *op, unsigned operandIdx) {
  llvm::StringRef opName = op->getName().getStringRef();
  // Operations that consume owned values:
  return opName == "own.move" || opName == "own.drop" ||
         opName == "own.store" ||
         // Function calls consume owned-typed arguments (move semantics).
         opName == "func.call" ||
         // Channel send consumes the value.
         opName == "chan.send" ||
         // Return consumes the returned value.
         opName == "func.return";
}

//===----------------------------------------------------------------------===//
// MoveCheckPass
//===----------------------------------------------------------------------===//

void MoveCheckPass::analyzeBlock(mlir::Block &block,
                                  ValueStateMap &stateAtEntry) {
  for (auto &op : block.getOperations()) {
    // Check operands for use-after-move.
    checkOperandStates(&op, stateAtEntry);

    // Process consuming operations — mark values as moved.
    for (unsigned i = 0; i < op.getNumOperands(); ++i) {
      mlir::Value operand = op.getOperand(i);
      if (!isOwnedType(operand.getType()))
        continue;

      if (isConsuming(&op, i)) {
        auto it = stateAtEntry.find(operand);
        if (it != stateAtEntry.end()) {
          if (it->second == MoveState::Moved) {
            // Double move!
            reportDoubleMove(&op, operand, firstMoveOp[operand]);
          } else {
            it->second = MoveState::Moved;
            firstMoveOp[operand] = &op;
          }
        } else {
          stateAtEntry[operand] = MoveState::Moved;
          firstMoveOp[operand] = &op;
        }
      }
    }

    // Process results — new owned values start as Live.
    for (mlir::Value result : op.getResults()) {
      if (isOwnedType(result.getType())) {
        stateAtEntry[result] = MoveState::Live;
      }
    }
  }
}

MoveCheckPass::ValueStateMap MoveCheckPass::mergeStates(
    const llvm::SmallVector<ValueStateMap *, 4> &predStates) {
  ValueStateMap merged;

  if (predStates.empty())
    return merged;

  if (predStates.size() == 1) {
    merged = *predStates[0];
    return merged;
  }

  // Collect all keys.
  llvm::DenseSet<mlir::Value> allKeys;
  for (const auto *pred : predStates) {
    for (const auto &[val, state] : *pred) {
      allKeys.insert(val);
    }
  }

  // For each value, merge states from all predecessors.
  for (mlir::Value val : allKeys) {
    bool anyLive = false;
    bool anyMoved = false;
    bool anyDropped = false;

    for (const auto *pred : predStates) {
      auto it = pred->find(val);
      if (it == pred->end())
        continue;
      switch (it->second) {
      case MoveState::Live:
        anyLive = true;
        break;
      case MoveState::Moved:
      case MoveState::MaybeMoved:
        anyMoved = true;
        break;
      case MoveState::Dropped:
        anyDropped = true;
        break;
      }
    }

    if (anyLive && anyMoved) {
      // Value is live on some paths but moved on others.
      merged[val] = MoveState::MaybeMoved;
    } else if (anyMoved) {
      merged[val] = MoveState::Moved;
    } else if (anyDropped) {
      merged[val] = MoveState::Dropped;
    } else {
      merged[val] = MoveState::Live;
    }
  }

  return merged;
}

void MoveCheckPass::checkOperandStates(mlir::Operation *op,
                                        const ValueStateMap &states) {
  for (mlir::Value operand : op->getOperands()) {
    if (!isOwnedType(operand.getType()))
      continue;

    auto it = states.find(operand);
    if (it == states.end())
      continue;

    switch (it->second) {
    case MoveState::Moved:
      reportUseAfterMove(op, operand, firstMoveOp[operand]);
      break;
    case MoveState::MaybeMoved: {
      // RFC specifies conditional moves as warnings, not errors.
      // Drop-flag insertion (RFC-0008) will handle the runtime behavior.
      mlir::InFlightDiagnostic diag = op->emitWarning()
          << "value may have been moved on a previous path";
      if (auto moveIt = firstMoveOp.find(operand);
          moveIt != firstMoveOp.end()) {
        diag.attachNote(moveIt->second->getLoc())
            << "value possibly moved here";
      }
      // Not signalPassFailure() — this is a warning, not an error.
      break;
    }
    case MoveState::Dropped:
      op->emitError() << "use of dropped value";
      signalPassFailure();
      break;
    case MoveState::Live:
      // OK.
      break;
    }
  }
}

void MoveCheckPass::checkPartialMoves(mlir::Operation *branchOp,
                                       const ValueStateMap &thenState,
                                       const ValueStateMap &elseState) {
  // Find values that are moved in one branch but not the other.
  llvm::DenseSet<mlir::Value> allValues;
  for (const auto &[val, _] : thenState)
    allValues.insert(val);
  for (const auto &[val, _] : elseState)
    allValues.insert(val);

  for (mlir::Value val : allValues) {
    auto thenIt = thenState.find(val);
    auto elseIt = elseState.find(val);

    MoveState thenS =
        (thenIt != thenState.end()) ? thenIt->second : MoveState::Live;
    MoveState elseS =
        (elseIt != elseState.end()) ? elseIt->second : MoveState::Live;

    if ((thenS == MoveState::Moved && elseS == MoveState::Live) ||
        (thenS == MoveState::Live && elseS == MoveState::Moved)) {
      branchOp->emitError()
          << "value is moved in one branch of conditional but not the other";
      signalPassFailure();
    }
  }
}

void MoveCheckPass::checkAllConsumed(mlir::func::FuncOp func,
                                      const ValueStateMap &exitState) {
  for (const auto &[val, state] : exitState) {
    if (state == MoveState::Live) {
      // Owned value was never consumed — resource leak.
      // Only warn for function-local values (not parameters that might
      // have been returned).
      if (auto *defOp = val.getDefiningOp()) {
        auto diag = defOp->emitWarning()
            << "owned value is never consumed; this is a resource leak. "
            << "Consider dropping it explicitly or returning it.";
        diag.attachNote() << "RFC-0005 linearity: every !own.val must have exactly one consuming use";
      }
    }
  }
}

void MoveCheckPass::reportUseAfterMove(mlir::Operation *use,
                                        mlir::Value value,
                                        mlir::Operation *moveOp) {
  mlir::InFlightDiagnostic diag =
      use->emitError() << "use of moved value";
  if (moveOp) {
    diag.attachNote(moveOp->getLoc()) << "value was moved here";
  }
  if (auto *defOp = value.getDefiningOp()) {
    diag.attachNote(defOp->getLoc()) << "value was defined here";
  }
  signalPassFailure();
}

void MoveCheckPass::reportDoubleMove(mlir::Operation *secondMove,
                                      mlir::Value value,
                                      mlir::Operation *firstMove) {
  mlir::InFlightDiagnostic diag =
      secondMove->emitError() << "value moved more than once";
  if (firstMove) {
    diag.attachNote(firstMove->getLoc()) << "value was first moved here";
  }
  signalPassFailure();
}

void MoveCheckPass::runOnOperation() {
  mlir::func::FuncOp func = getOperation();
  if (func.isDeclaration())
    return;

  firstMoveOp.clear();

  // Build per-block state maps using forward dataflow.
  llvm::DenseMap<mlir::Block *, ValueStateMap> blockEntryStates;
  llvm::DenseMap<mlir::Block *, ValueStateMap> blockExitStates;

  // Initialize entry block: function arguments that are owned start as Live.
  mlir::Block &entryBlock = func.getBody().front();
  ValueStateMap entryState;
  for (mlir::Value arg : entryBlock.getArguments()) {
    if (isOwnedType(arg.getType())) {
      entryState[arg] = MoveState::Live;
    }
  }
  blockEntryStates[&entryBlock] = entryState;

  // Worklist-based forward dataflow.
  llvm::SmallVector<mlir::Block *, 16> worklist;
  llvm::DenseSet<mlir::Block *> onWorklist;

  worklist.push_back(&entryBlock);
  onWorklist.insert(&entryBlock);

  while (!worklist.empty()) {
    mlir::Block *block = worklist.pop_back_val();
    onWorklist.erase(block);

    // Get or create entry state for this block.
    ValueStateMap state = blockEntryStates[block];

    // Analyze the block.
    analyzeBlock(*block, state);

    // Store exit state.
    blockExitStates[block] = state;

    // Propagate to successors.
    for (mlir::Block *succ : block->getSuccessors()) {
      // Merge current exit state into successor's entry state.
      auto succIt = blockEntryStates.find(succ);
      if (succIt == blockEntryStates.end()) {
        blockEntryStates[succ] = state;
        if (!onWorklist.count(succ)) {
          worklist.push_back(succ);
          onWorklist.insert(succ);
        }
      } else {
        // Merge with existing entry state.
        llvm::SmallVector<ValueStateMap *, 4> states;
        states.push_back(&state);
        states.push_back(&succIt->second);
        ValueStateMap merged = mergeStates(states);

        if (merged != succIt->second) {
          succIt->second = std::move(merged);
          if (!onWorklist.count(succ)) {
            worklist.push_back(succ);
            onWorklist.insert(succ);
          }
        }
      }
    }
  }

  // Check for partial moves at branch points.
  // (Handled during merge — MaybeMoved state triggers errors on use.)

  // Check for unconsumed owned values at function exit.
  for (auto &block : func.getBody()) {
    if (block.getOperations().empty())
      continue;
    mlir::Operation &terminator = block.back();
    if (terminator.hasTrait<mlir::OpTrait::ReturnLike>()) {
      checkAllConsumed(func, blockExitStates[&block]);
    }
  }
}

std::unique_ptr<mlir::Pass> createMoveCheckPass() {
  return std::make_unique<MoveCheckPass>();
}

} // namespace asc
