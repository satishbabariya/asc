// LivenessAnalysis — backward dataflow liveness for ownership tracking.
//
// Computes live-in/live-out sets per basic block using iterative fixpoint:
//   liveIn(B) = uses(B) U (liveOut(B) - defs(B))
//   liveOut(B) = U { liveIn(S) | S in successors(B) }

#include "asc/Analysis/LivenessAnalysis.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// LivenessResult
//===----------------------------------------------------------------------===//

const BlockLiveness &
LivenessResult::getBlockLiveness(mlir::Block *block) const {
  auto it = blockInfo.find(block);
  assert(it != blockInfo.end() && "Block not found in liveness result");
  return it->second;
}

bool LivenessResult::isLiveAt(mlir::Value value, mlir::Operation *op) const {
  mlir::Block *block = op->getBlock();
  auto it = blockInfo.find(block);
  if (it == blockInfo.end())
    return false;

  const auto &info = it->second;

  // If the value is live-out, it's live at every point after its definition.
  // Walk backward from block terminator to the operation.
  // A value is live at an op if it is used at or after that op in the block,
  // or if it is live-out.
  if (info.liveOut.count(value))
    return true;

  // Walk forward from op — if we find a use before a def, it's live.
  bool foundOp = false;
  for (auto &blockOp : block->getOperations()) {
    if (&blockOp == op)
      foundOp = true;
    if (!foundOp)
      continue;

    // Check if this operation uses the value.
    for (mlir::Value operand : blockOp.getOperands()) {
      if (operand == value)
        return true;
    }

    // Check if this operation defines (kills) the value.
    for (mlir::Value result : blockOp.getResults()) {
      if (result == value)
        return false; // Redefined — not live before this point.
    }
  }

  return false;
}

bool LivenessResult::isLiveIn(mlir::Value value, mlir::Block *block) const {
  auto it = blockInfo.find(block);
  if (it == blockInfo.end())
    return false;
  return it->second.liveIn.count(value) != 0;
}

bool LivenessResult::isLiveOut(mlir::Value value, mlir::Block *block) const {
  auto it = blockInfo.find(block);
  if (it == blockInfo.end())
    return false;
  return it->second.liveOut.count(value) != 0;
}

//===----------------------------------------------------------------------===//
// LivenessAnalysisPass
//===----------------------------------------------------------------------===//

void LivenessAnalysisPass::computeLocalSets(mlir::Block &block,
                                             BlockLiveness &info) {
  info.defs.clear();
  info.uses.clear();

  // Walk the block forward to compute gen (uses) and kill (defs) sets.
  // A value is in 'uses' if it is used before being defined in this block.
  // A value is in 'defs' if it is defined in this block.
  for (auto &op : block.getOperations()) {
    // Process operands — these are uses.
    for (mlir::Value operand : op.getOperands()) {
      // Only track if not defined earlier in this block.
      if (!info.defs.count(operand)) {
        info.uses.insert(operand);
      }
    }

    // Process results — these are definitions.
    for (mlir::Value result : op.getResults()) {
      info.defs.insert(result);
    }
  }

  // Block arguments are definitions.
  for (mlir::Value arg : block.getArguments()) {
    info.defs.insert(arg);
  }
}

void LivenessAnalysisPass::solveDataflow(mlir::func::FuncOp func) {
  // Initialize block info and compute local gen/kill sets.
  for (auto &block : func.getBody()) {
    auto &info = result.blockInfo[&block];
    computeLocalSets(block, info);
  }

  // Collect blocks in reverse post-order for the backward analysis.
  // We iterate in post-order (natural for backward analysis).
  llvm::SmallVector<mlir::Block *, 16> postOrder;
  for (auto &block : func.getBody()) {
    postOrder.push_back(&block);
  }
  // Reverse to get a rough post-order (for structured CFGs, iterating
  // blocks in reverse order is a reasonable approximation).
  std::reverse(postOrder.begin(), postOrder.end());

  // Iterative fixpoint computation.
  bool changed = true;
  while (changed) {
    changed = false;

    for (mlir::Block *block : postOrder) {
      auto &info = result.blockInfo[block];

      // Compute liveOut = union of liveIn of all successors.
      llvm::DenseSet<mlir::Value> newLiveOut;
      for (mlir::Block *succ : block->getSuccessors()) {
        const auto &succInfo = result.blockInfo[succ];
        for (mlir::Value v : succInfo.liveIn) {
          newLiveOut.insert(v);
        }
      }

      // Compute liveIn = uses U (liveOut - defs).
      llvm::DenseSet<mlir::Value> newLiveIn;
      // Start with uses (gen set).
      for (mlir::Value v : info.uses) {
        newLiveIn.insert(v);
      }
      // Add liveOut values that are not killed in this block.
      for (mlir::Value v : newLiveOut) {
        if (!info.defs.count(v)) {
          newLiveIn.insert(v);
        }
      }

      // Check if anything changed.
      if (newLiveIn.size() != info.liveIn.size() ||
          newLiveOut.size() != info.liveOut.size()) {
        changed = true;
      } else {
        // Check actual set contents.
        for (mlir::Value v : newLiveIn) {
          if (!info.liveIn.count(v)) {
            changed = true;
            break;
          }
        }
        if (!changed) {
          for (mlir::Value v : newLiveOut) {
            if (!info.liveOut.count(v)) {
              changed = true;
              break;
            }
          }
        }
      }

      info.liveIn = std::move(newLiveIn);
      info.liveOut = std::move(newLiveOut);
    }
  }
}

void LivenessAnalysisPass::runOnOperation() {
  mlir::func::FuncOp func = getOperation();

  // Skip declarations (no body).
  if (func.isDeclaration())
    return;

  // Clear any previous results.
  result.blockInfo.clear();
  result.liveAtExit.clear();

  // Run the iterative dataflow solver.
  solveDataflow(func);

  // The live-at-exit set is the liveOut of the entry block's predecessors...
  // Actually, for function analysis, "live at exit" means values live-out
  // of blocks that contain return operations.
  for (auto &block : func.getBody()) {
    if (block.getOperations().empty())
      continue;
    mlir::Operation &terminator = block.back();
    if (terminator.hasTrait<mlir::OpTrait::ReturnLike>()) {
      const auto &info = result.blockInfo[&block];
      for (mlir::Value v : info.liveOut) {
        result.liveAtExit.insert(v);
      }
    }
  }
}

std::unique_ptr<mlir::Pass> createLivenessAnalysisPass() {
  return std::make_unique<LivenessAnalysisPass>();
}

} // namespace asc
