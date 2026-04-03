#ifndef ASC_ANALYSIS_LIVENESSANALYSIS_H
#define ASC_ANALYSIS_LIVENESSANALYSIS_H

#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include <memory>

namespace asc {

/// Per-block liveness information computed by backward dataflow analysis.
struct BlockLiveness {
  /// Values live at entry to this block.
  llvm::DenseSet<mlir::Value> liveIn;
  /// Values live at exit of this block.
  llvm::DenseSet<mlir::Value> liveOut;
  /// Values defined (killed) in this block.
  llvm::DenseSet<mlir::Value> defs;
  /// Values used (generated) before any definition in this block.
  llvm::DenseSet<mlir::Value> uses;
};

/// Results of liveness analysis for a single function.
class LivenessResult {
public:
  /// Get liveness info for a specific block.
  const BlockLiveness &getBlockLiveness(mlir::Block *block) const;

  /// Check if a value is live at a particular operation.
  bool isLiveAt(mlir::Value value, mlir::Operation *op) const;

  /// Check if a value is live-in to a block.
  bool isLiveIn(mlir::Value value, mlir::Block *block) const;

  /// Check if a value is live-out of a block.
  bool isLiveOut(mlir::Value value, mlir::Block *block) const;

  /// Get all values that are live at the exit of the function.
  const llvm::DenseSet<mlir::Value> &getLiveAtExit() const {
    return liveAtExit;
  }

private:
  friend class LivenessAnalysisPass;
  llvm::DenseMap<mlir::Block *, BlockLiveness> blockInfo;
  llvm::DenseSet<mlir::Value> liveAtExit;
};

/// Backward dataflow liveness analysis pass operating on MLIR func ops.
///
/// Computes live-in and live-out sets for each basic block using a standard
/// iterative backward dataflow algorithm:
///   liveIn(B) = uses(B) U (liveOut(B) - defs(B))
///   liveOut(B) = U { liveIn(S) | S in successors(B) }
///
/// This information is consumed by subsequent borrow-checker passes
/// (RegionInference, AliasCheck, DropInsertion).
class LivenessAnalysisPass
    : public mlir::PassWrapper<LivenessAnalysisPass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LivenessAnalysisPass)

  llvm::StringRef getArgument() const override {
    return "asc-liveness-analysis";
  }
  llvm::StringRef getDescription() const override {
    return "Backward dataflow liveness analysis for ownership tracking";
  }

  void runOnOperation() override;

  /// Retrieve the computed liveness result (valid after the pass runs).
  const LivenessResult &getResult() const { return result; }

private:
  /// Compute gen/kill sets for a single block.
  void computeLocalSets(mlir::Block &block, BlockLiveness &info);

  /// Run the iterative fixpoint solver over all blocks.
  void solveDataflow(mlir::func::FuncOp func);

  LivenessResult result;
};

/// Create a liveness analysis pass.
std::unique_ptr<mlir::Pass> createLivenessAnalysisPass();

} // namespace asc

#endif // ASC_ANALYSIS_LIVENESSANALYSIS_H
