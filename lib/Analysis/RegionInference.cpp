// RegionInference — NLL-style borrow region inference using union-find.
//
// Assigns borrow regions and expands them based on liveness. Uses union-find
// to efficiently merge regions at phi nodes and assignments.

#include "asc/Analysis/RegionInference.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Value.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// RegionUnionFind
//===----------------------------------------------------------------------===//

RegionID RegionUnionFind::makeRegion() {
  RegionID id = parent.size();
  parent.push_back(id);
  rank.push_back(0);
  return id;
}

RegionID RegionUnionFind::find(RegionID id) {
  // Path compression.
  while (parent[id] != id) {
    parent[id] = parent[parent[id]]; // Path halving.
    id = parent[id];
  }
  return id;
}

void RegionUnionFind::merge(RegionID a, RegionID b) {
  a = find(a);
  b = find(b);
  if (a == b)
    return;

  // Union by rank.
  if (rank[a] < rank[b])
    std::swap(a, b);
  parent[b] = a;
  if (rank[a] == rank[b])
    ++rank[a];
}

bool RegionUnionFind::sameRegion(RegionID a, RegionID b) {
  return find(a) == find(b);
}

//===----------------------------------------------------------------------===//
// RegionInferenceResult
//===----------------------------------------------------------------------===//

RegionID RegionInferenceResult::getRegion(mlir::Value borrowValue) const {
  auto it = valueToRegion.find(borrowValue);
  assert(it != valueToRegion.end() && "Value not found in region map");
  return it->second;
}

const BorrowRegion &
RegionInferenceResult::getBorrowRegion(RegionID id) const {
  assert(id < regions.size() && "Region ID out of range");
  return regions[id];
}

bool RegionInferenceResult::isLiveAt(RegionID region,
                                      CFGPoint point) const {
  assert(region < regions.size() && "Region ID out of range");
  const auto &reg = regions[region];
  for (const auto &p : reg.points) {
    if (p == point)
      return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// RegionInferencePass
//===----------------------------------------------------------------------===//

void RegionInferencePass::buildCFGIndex(mlir::func::FuncOp func) {
  blockIndex.clear();
  unsigned idx = 0;
  for (auto &block : func.getBody()) {
    blockIndex[&block] = idx++;
  }
}

void RegionInferencePass::assignInitialRegions(mlir::func::FuncOp func) {
  // Walk all operations looking for borrow-creating ops.
  func.walk([&](mlir::Operation *op) {
    llvm::StringRef opName = op->getName().getStringRef();

    bool isBorrow = opName == "own.borrow" || opName == "own.borrow_mut";
    if (!isBorrow)
      return;

    // Each borrow op produces a result that is the borrow value.
    if (op->getNumResults() == 0)
      return;

    mlir::Value borrowVal = op->getResult(0);
    bool isMutable = (opName == "own.borrow_mut");

    // Get the origin value (the owned value being borrowed).
    mlir::Value originVal;
    if (op->getNumOperands() > 0)
      originVal = op->getOperand(0);

    // Create a new region for this borrow.
    RegionID regionId = result.unionFind.makeRegion();

    // Create the borrow region with the definition point.
    BorrowRegion region;
    region.id = regionId;
    region.borrowedValue = originVal;
    region.isMutable = isMutable;

    // Add the definition point.
    mlir::Block *defBlock = op->getBlock();
    unsigned blockIdx = blockIndex[defBlock];
    unsigned stmtIdx = 0;
    for (auto &blockOp : defBlock->getOperations()) {
      if (&blockOp == op)
        break;
      ++stmtIdx;
    }
    region.points.push_back(CFGPoint{blockIdx, stmtIdx});

    // Ensure regions vector is large enough.
    if (result.regions.size() <= regionId)
      result.regions.resize(regionId + 1);
    result.regions[regionId] = std::move(region);
    result.valueToRegion[borrowVal] = regionId;
  });
}

void RegionInferencePass::extendRegionsToUses(mlir::func::FuncOp func) {
  // For each borrow value, find all uses and extend the region to cover
  // the path from definition to each use.
  for (auto &[borrowVal, regionId] : result.valueToRegion) {
    auto &region = result.regions[regionId];

    for (mlir::OpOperand &use : borrowVal.getUses()) {
      mlir::Operation *useOp = use.getOwner();
      mlir::Block *useBlock = useOp->getBlock();
      unsigned useBlockIdx = blockIndex[useBlock];

      // Find the statement index of the use within its block.
      unsigned useStmtIdx = 0;
      for (auto &blockOp : useBlock->getOperations()) {
        if (&blockOp == useOp)
          break;
        ++useStmtIdx;
      }

      CFGPoint usePoint{useBlockIdx, useStmtIdx};

      // Add all points between definition and use to the region.
      // For now, add just the use point. A full NLL implementation would
      // compute the minimal path through the CFG.
      bool alreadyPresent = false;
      for (const auto &p : region.points) {
        if (p == usePoint) {
          alreadyPresent = true;
          break;
        }
      }
      if (!alreadyPresent)
        region.points.push_back(usePoint);

      // If the use is in a different block than the definition, extend
      // the region to cover all intermediate blocks on the CFG path.
      if (!region.points.empty()) {
        unsigned defBlockIdx = region.points[0].blockIndex;
        if (useBlockIdx != defBlockIdx) {
          // Add entry/exit points for all blocks between def and use.
          // Walk forward through blocks (simplified — assumes linear CFG).
          unsigned lo = std::min(defBlockIdx, useBlockIdx);
          unsigned hi = std::max(defBlockIdx, useBlockIdx);
          for (unsigned b = lo; b <= hi; ++b) {
            CFGPoint entry{b, 0};
            bool found = false;
            for (const auto &p : region.points) {
              if (p.blockIndex == b) {
                found = true;
                break;
              }
            }
            if (!found)
              region.points.push_back(entry);
          }
        }
      }
    }
  }
}

void RegionInferencePass::propagateThroughPhis(mlir::func::FuncOp func) {
  // For each block with arguments (phi nodes), if a block argument is a
  // borrow, unify the regions of all incoming values that feed into it.
  for (auto &block : func.getBody()) {
    for (unsigned argIdx = 0; argIdx < block.getNumArguments(); ++argIdx) {
      mlir::Value blockArg = block.getArgument(argIdx);

      // Check if this block argument has a region (is a borrow).
      auto argIt = result.valueToRegion.find(blockArg);

      // Collect incoming values for this block argument from predecessors.
      for (auto *pred : block.getPredecessors()) {
        mlir::Operation *terminator = pred->getTerminator();
        if (!terminator)
          continue;

        // For branch-like ops, get the operand corresponding to this
        // block argument.
        // The successor operands tell us which values flow into the
        // block argument.
        for (unsigned succIdx = 0;
             succIdx < terminator->getNumSuccessors(); ++succIdx) {
          if (terminator->getSuccessor(succIdx) != &block)
            continue;

          // Get successor operands for this edge.
          auto succOperands =
              terminator->getSuccessorOperands(succIdx);
          if (argIdx < succOperands.size()) {
            mlir::Value incomingVal = succOperands[argIdx];
            auto inIt = result.valueToRegion.find(incomingVal);

            if (argIt != result.valueToRegion.end() &&
                inIt != result.valueToRegion.end()) {
              // Both the block arg and the incoming value have regions.
              // Merge them.
              result.unionFind.merge(argIt->second, inIt->second);
            } else if (inIt != result.valueToRegion.end() &&
                       argIt == result.valueToRegion.end()) {
              // The incoming value has a region; assign it to the block arg.
              result.valueToRegion[blockArg] = inIt->second;
              argIt = result.valueToRegion.find(blockArg);
            }
          }
        }
      }
    }
  }
}

void RegionInferencePass::runOnOperation() {
  mlir::func::FuncOp func = getOperation();
  if (func.isDeclaration())
    return;

  // Clear previous results.
  result.valueToRegion.clear();
  result.regions.clear();
  blockIndex.clear();

  // Step 1: Build CFG index.
  buildCFGIndex(func);

  // Step 2: Assign initial regions to each borrow operation.
  assignInitialRegions(func);

  // Step 3: Extend regions to cover all use points.
  extendRegionsToUses(func);

  // Step 4: Propagate through phi nodes (block arguments).
  propagateThroughPhis(func);
}

std::unique_ptr<mlir::Pass> createRegionInferencePass() {
  return std::make_unique<RegionInferencePass>();
}

} // namespace asc
