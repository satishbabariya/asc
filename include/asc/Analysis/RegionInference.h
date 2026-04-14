#ifndef ASC_ANALYSIS_REGIONINFERENCE_H
#define ASC_ANALYSIS_REGIONINFERENCE_H

#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace asc {

/// Unique identifier for a borrow region (NLL-style).
using RegionID = uint32_t;

/// A point in the CFG identified by (block index, statement index).
struct CFGPoint {
  unsigned blockIndex;
  unsigned stmtIndex;

  bool operator==(const CFGPoint &other) const {
    return blockIndex == other.blockIndex && stmtIndex == other.stmtIndex;
  }
  bool operator<(const CFGPoint &other) const {
    if (blockIndex != other.blockIndex)
      return blockIndex < other.blockIndex;
    return stmtIndex < other.stmtIndex;
  }
};

/// A borrow region is a set of CFG points where a borrow is active.
/// Implemented using a union-find structure for efficient merging.
struct BorrowRegion {
  RegionID id;
  /// The set of CFG points contained in this region.
  llvm::SmallVector<CFGPoint, 8> points;
  /// The value that was borrowed.
  mlir::Value borrowedValue;
  /// Whether this is a mutable borrow.
  bool isMutable = false;
};

/// An outlives constraint: borrow region must not outlive the scope of its origin.
struct OutlivesConstraint {
  RegionID borrowRegion;  // The borrow that must end first
  mlir::Value origin;     // The value being borrowed
  mlir::Location loc;     // Source location for diagnostics
};

/// Union-Find (disjoint set) data structure for region merging.
class RegionUnionFind {
public:
  /// Create a new region and return its ID.
  RegionID makeRegion();

  /// Find the representative region ID (with path compression).
  RegionID find(RegionID id);

  /// Merge two regions into one.
  void merge(RegionID a, RegionID b);

  /// Check if two region IDs belong to the same region.
  bool sameRegion(RegionID a, RegionID b);

  /// Get the total number of distinct regions.
  unsigned numRegions() const { return parent.size(); }

private:
  llvm::SmallVector<RegionID, 32> parent;
  llvm::SmallVector<unsigned, 32> rank;
};

/// Result of region inference for a function.
class RegionInferenceResult {
public:
  /// Get the region assigned to a borrow value.
  RegionID getRegion(mlir::Value borrowValue) const;

  /// Get the borrow region data for a region ID.
  const BorrowRegion &getBorrowRegion(RegionID id) const;

  /// Get all borrow regions.
  const llvm::SmallVector<BorrowRegion, 16> &getAllRegions() const {
    return regions;
  }

  /// Check if a borrow is live at a given CFG point.
  bool isLiveAt(RegionID region, CFGPoint point) const;

  /// Get the region for a borrow value, if assigned.
  std::optional<RegionID> getRegionForBorrow(mlir::Value borrowVal) const {
    auto it = valueToRegion.find(borrowVal);
    if (it != valueToRegion.end())
      return it->second;
    return std::nullopt;
  }

  /// Get all outlives constraints collected during inference.
  const llvm::SmallVector<OutlivesConstraint, 8> &getOutlives() const {
    return outlives;
  }

private:
  friend class RegionInferencePass;
  llvm::DenseMap<mlir::Value, RegionID> valueToRegion;
  llvm::SmallVector<BorrowRegion, 16> regions;
  llvm::SmallVector<OutlivesConstraint, 8> outlives;
  RegionUnionFind unionFind;
};

/// NLL-style region inference pass.
///
/// Assigns each borrow a region (set of CFG points) and expands regions
/// based on liveness constraints. Uses a union-find structure to merge
/// regions that must be equal due to assignments and phi nodes.
///
/// Algorithm:
/// 1. Assign initial regions to each own.borrow / own.borrow_mut op.
/// 2. For each use of a borrow, extend the region to cover the use point.
/// 3. Propagate region constraints through control flow (phi merges).
/// 4. Minimize regions by removing points after last use.
class RegionInferencePass
    : public mlir::PassWrapper<RegionInferencePass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(RegionInferencePass)

  llvm::StringRef getArgument() const override {
    return "asc-region-inference";
  }
  llvm::StringRef getDescription() const override {
    return "NLL-style borrow region inference using union-find";
  }

  void runOnOperation() override;

  const RegionInferenceResult &getResult() const { return result; }

private:
  /// Assign an initial region to each borrow operation.
  void assignInitialRegions(mlir::func::FuncOp func);

  /// Extend regions to cover all use points of each borrow.
  void extendRegionsToUses(mlir::func::FuncOp func);

  /// Propagate region constraints through block arguments (phi nodes).
  void propagateThroughPhis(mlir::func::FuncOp func);

  /// Collect outlives constraints (borrows that escape their origin's scope).
  void collectOutlivesConstraints(mlir::func::FuncOp func);

  /// Validate outlives constraints and emit E007 diagnostics.
  void validateOutlives(mlir::func::FuncOp func);

  /// Build the CFG point index for blocks.
  void buildCFGIndex(mlir::func::FuncOp func);

  /// Map from blocks to their index in the CFG.
  llvm::DenseMap<mlir::Block *, unsigned> blockIndex;

  RegionInferenceResult result;
};

/// Create a region inference pass.
std::unique_ptr<mlir::Pass> createRegionInferencePass();

} // namespace asc

#endif // ASC_ANALYSIS_REGIONINFERENCE_H
