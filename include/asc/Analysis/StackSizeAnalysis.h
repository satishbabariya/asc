#ifndef ASC_ANALYSIS_STACKSIZEANALYSIS_H
#define ASC_ANALYSIS_STACKSIZEANALYSIS_H

#include "mlir/Pass/Pass.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>

namespace asc {

class StackSizeAnalysisPass
    : public mlir::PassWrapper<StackSizeAnalysisPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StackSizeAnalysisPass)

  llvm::StringRef getArgument() const override {
    return "asc-stack-size-analysis";
  }
  llvm::StringRef getDescription() const override {
    return "Conservative stack size analysis for spawned tasks";
  }

  void runOnOperation() override;

private:
  /// Estimate stack usage from LLVM alloca ops inside a func::FuncOp.
  uint64_t estimateStackUsage(mlir::func::FuncOp func);

  /// Estimate stack usage from LLVM alloca ops inside an LLVMFuncOp.
  uint64_t estimateStackUsageLLVM(mlir::LLVM::LLVMFuncOp func);

  /// Walk the call graph from a func::FuncOp, summing local + callee stacks.
  uint64_t walkCallGraph(mlir::func::FuncOp func,
                         llvm::DenseSet<mlir::Operation *> &visited);

  /// Walk the call graph from an LLVMFuncOp (task wrapper entry point).
  uint64_t walkCallGraphLLVM(mlir::LLVM::LLVMFuncOp func,
                             llvm::DenseSet<mlir::Operation *> &visited);

  static constexpr uint64_t DEFAULT_STACK_LIMIT = 1024 * 1024; // 1 MB
};

std::unique_ptr<mlir::Pass> createStackSizeAnalysisPass();

} // namespace asc

#endif
