#ifndef ASC_ANALYSIS_ESCAPEANALYSIS_H
#define ASC_ANALYSIS_ESCAPEANALYSIS_H

#include "mlir/Pass/Pass.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/DenseMap.h"
#include <memory>

namespace asc {

enum class EscapeStatus {
  StackSafe,  // All uses local — can stay on stack
  MustHeap,   // Escapes scope — must use heap allocation
  Unknown     // Cannot determine — conservative: use heap
};

class EscapeAnalysisResult {
public:
  EscapeStatus getStatus(mlir::Operation *allocOp) const {
    auto it = allocStatus.find(allocOp);
    return (it != allocStatus.end()) ? it->second : EscapeStatus::Unknown;
  }
  void setStatus(mlir::Operation *allocOp, EscapeStatus status) {
    allocStatus[allocOp] = status;
  }
private:
  llvm::DenseMap<mlir::Operation *, EscapeStatus> allocStatus;
};

class EscapeAnalysisPass
    : public mlir::PassWrapper<EscapeAnalysisPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(EscapeAnalysisPass)
  llvm::StringRef getArgument() const override { return "asc-escape-analysis"; }
  llvm::StringRef getDescription() const override {
    return "Classify own.alloc ops as stack-safe or must-heap";
  }
  void runOnOperation() override;
  const EscapeAnalysisResult &getResult() const { return result; }
private:
  bool escapesThroughUses(mlir::Value val);
  EscapeAnalysisResult result;
};

std::unique_ptr<mlir::Pass> createEscapeAnalysisPass();

} // namespace asc
#endif
