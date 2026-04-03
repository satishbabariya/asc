#ifndef ASC_CODEGEN_CONCURRENCYLOWERING_H
#define ASC_CODEGEN_CONCURRENCYLOWERING_H

#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>

namespace asc {

/// Concurrency lowering pass — converts task.* and chan.* ops to LLVM dialect.
///
/// Lowering depends on the target triple:
///
/// **wasm32-wasi-threads:**
/// - task.spawn   → wasi-threads thread_spawn (or __wasi_thread_spawn)
/// - task.join    → atomic wait + shared memory synchronization
/// - chan.make    → shared linear memory allocation + atomics
/// - chan.send    → atomic store + memory.atomic.notify
/// - chan.recv    → memory.atomic.wait32 + atomic load
/// - chan.close   → atomic flag set
///
/// **Native (x86_64, aarch64, etc.):**
/// - task.spawn   → pthread_create wrapper
/// - task.join    → pthread_join
/// - chan.make    → heap allocation + mutex + condvar
/// - chan.send    → mutex lock + enqueue + condvar signal
/// - chan.recv    → mutex lock + condvar wait + dequeue
/// - chan.close   → flag set + broadcast
class ConcurrencyLoweringPass
    : public mlir::PassWrapper<ConcurrencyLoweringPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ConcurrencyLoweringPass)

  ConcurrencyLoweringPass() = default;
  explicit ConcurrencyLoweringPass(llvm::Triple targetTriple)
      : targetTriple(std::move(targetTriple)) {}

  llvm::StringRef getArgument() const override {
    return "asc-concurrency-lowering";
  }
  llvm::StringRef getDescription() const override {
    return "Lower task.* and chan.* dialect ops based on target triple";
  }

  void runOnOperation() override;

private:
  /// Set up the conversion target.
  void configureTarget(mlir::ConversionTarget &target);

  /// Populate patterns for Wasm target.
  void populateWasmPatterns(mlir::RewritePatternSet &patterns,
                            mlir::TypeConverter &typeConverter);

  /// Populate patterns for native (pthreads) target.
  void populateNativePatterns(mlir::RewritePatternSet &patterns,
                              mlir::TypeConverter &typeConverter);

  /// Declare external runtime functions (malloc, pthread_create, etc.).
  void declareRuntimeFunctions(mlir::ModuleOp module);

  /// Declare WASI-threads specific imports.
  void declareWasiThreadsFunctions(mlir::ModuleOp module);

  /// Check if we are targeting Wasm.
  bool isWasmTarget() const {
    return targetTriple.getArch() == llvm::Triple::wasm32 ||
           targetTriple.getArch() == llvm::Triple::wasm64;
  }

  llvm::Triple targetTriple;
};

/// Create the concurrency lowering pass.
std::unique_ptr<mlir::Pass> createConcurrencyLoweringPass();
std::unique_ptr<mlir::Pass>
createConcurrencyLoweringPass(llvm::Triple targetTriple);

} // namespace asc

#endif // ASC_CODEGEN_CONCURRENCYLOWERING_H
