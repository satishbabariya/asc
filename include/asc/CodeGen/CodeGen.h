#ifndef ASC_CODEGEN_CODEGEN_H
#define ASC_CODEGEN_CODEGEN_H

#include "mlir/IR/BuiltinOps.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>

namespace asc {

/// Emit format for the code generator.
enum class EmitKind {
  Wasm,   // WebAssembly binary (.wasm)
  Wat,    // WebAssembly text format (.wat)
  LLVMIR, // LLVM IR text (.ll)
  MLIR,   // MLIR text (for debugging)
  Object, // Native object file (.o)
  Asm,    // Native assembly (.s)
};

/// Optimization level.
enum class OptLevel {
  O0, O1, O2, O3, Os, Oz,
};

/// Configuration for the code generator.
struct CodeGenOptions {
  /// Target triple (e.g., "wasm32-wasi-threads", "x86_64-linux-gnu").
  std::string targetTriple = "wasm32-wasi-threads";

  /// Output format.
  EmitKind emitKind = EmitKind::Wasm;

  /// Optimization level.
  OptLevel optLevel = OptLevel::O0;

  /// Whether to emit debug info.
  bool debugInfo = false;

  /// Output filename ("" means stdout).
  std::string outputFile;
};

/// Code generator — final stage of the compiler pipeline.
///
/// Takes an MLIR module (after all analysis and transform passes) and:
/// 1. Runs the ownership lowering pass (own.* → LLVM dialect)
/// 2. Runs the concurrency lowering pass (task.* → LLVM dialect)
/// 3. Runs the MLIR-to-LLVM-dialect conversion
/// 4. Translates the LLVM dialect to LLVM IR
/// 5. Runs LLVM optimization passes (per OptLevel)
/// 6. Emits the target code (Wasm, object file, assembly, etc.)
class CodeGenerator {
public:
  CodeGenerator(const CodeGenOptions &opts);
  ~CodeGenerator();

  /// Run the full lowering + codegen pipeline on an MLIR module.
  /// Returns 0 on success, non-zero on failure.
  int generate(mlir::ModuleOp module);

  /// Get the generated LLVM module (valid after generate() succeeds).
  llvm::Module *getLLVMModule() const { return llvmModule.get(); }

private:
  /// Run MLIR lowering passes (ownership, concurrency, std→llvm).
  bool runMLIRLowering(mlir::ModuleOp module);

  /// Translate MLIR LLVM dialect to actual LLVM IR.
  bool translateToLLVMIR(mlir::ModuleOp module);

  /// Configure and create the LLVM target machine.
  bool setupTargetMachine();

  /// Run LLVM optimization passes on the module.
  void runLLVMOptPasses();

  /// Emit the final output based on emitKind.
  bool emitOutput(llvm::raw_pwrite_stream &os);

  /// Emit MLIR text (before LLVM translation).
  bool emitMLIR(mlir::ModuleOp module, llvm::raw_ostream &os);

  /// Emit LLVM IR text.
  bool emitLLVMIR(llvm::raw_ostream &os);

  /// Emit object code or assembly via LLVM backend.
  bool emitMachineCode(llvm::raw_pwrite_stream &os, bool isAsm);

  /// Add DWARF debug info to LLVM module.
  void addDebugInfo();

  /// Map OptLevel to LLVM's CodeGenOptLevel.
  llvm::CodeGenOptLevel getLLVMOptLevel() const;

  /// Map OptLevel to LLVM's OptimizationLevel for the new pass manager.
  unsigned getLLVMOptLevelNum() const;

  CodeGenOptions opts;
  llvm::LLVMContext llvmContext;
  std::unique_ptr<llvm::Module> llvmModule;
  std::unique_ptr<llvm::TargetMachine> targetMachine;
};

} // namespace asc

#endif // ASC_CODEGEN_CODEGEN_H
