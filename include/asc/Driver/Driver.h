#ifndef ASC_DRIVER_DRIVER_H
#define ASC_DRIVER_DRIVER_H

#include "asc/Basic/Diagnostic.h"
#include "asc/Basic/SourceManager.h"
#include "asc/CodeGen/CodeGen.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include <string>
#include <vector>

namespace asc {

/// Exit codes for the asc compiler.
enum class ExitCode : int {
  Success = 0,       // Compilation succeeded
  CompileError = 1,  // Source-level errors (parse, type, borrow)
  SystemError = 2,   // I/O errors, missing files, internal errors
  UsageError = 3,    // Bad CLI arguments, unknown subcommand
};

/// Subcommand for the CLI.
enum class Subcommand {
  Build,  // Compile and produce output
  Check,  // Frontend + borrow checker only, no codegen
  Fmt,    // Format source
  Doc,    // Extract documentation
  Lsp,    // Start LSP server
};

/// Parsed command-line options.
struct DriverOptions {
  Subcommand subcommand = Subcommand::Build;
  std::string inputFile;
  std::string outputFile;
  std::string targetTriple = "wasm32-wasi-threads";
  EmitKind emitKind = EmitKind::Wasm;
  OptLevel optLevel = OptLevel::O0;
  bool debugInfo = false;
  ErrorFormat errorFormat = ErrorFormat::Human;
  bool verbose = false;
  bool printMLIR = false;   // --emit mlir
  bool printLLVMIR = false; // --emit llvmir
};

/// The driver ties the full compiler pipeline together.
///
/// Pipeline: Source → Lexer → Parser → AST → Sema → HIR → Analysis →
///           Transforms → Lowering → CodeGen
///
/// For the `check` subcommand, the pipeline stops after analysis (no codegen).
class Driver {
public:
  Driver();
  ~Driver();

  /// Parse command-line arguments and populate options.
  /// Returns UsageError on bad arguments, Success otherwise.
  ExitCode parseArgs(int argc, char **argv);

  /// Run the full compilation pipeline.
  ExitCode run();

  /// Get the parsed options (for testing).
  const DriverOptions &getOptions() const { return opts; }

private:
  /// Run the `build` subcommand: full compile to output.
  ExitCode runBuild();

  /// Run the `check` subcommand: frontend + analysis only.
  ExitCode runCheck();

  /// Run the `fmt` subcommand: format source file.
  ExitCode runFmt();

  /// Stage 1: Load source file.
  ExitCode loadSource();

  /// Stage 2: Lex and parse to AST.
  ExitCode parseSource();

  /// Stage 3: Run semantic analysis.
  ExitCode runSema();

  /// Stage 4: Lower AST to HIR (MLIR own/task dialect).
  ExitCode lowerToHIR();

  /// Stage 5: Run borrow-checker analysis passes.
  ExitCode runAnalysis();

  /// Stage 6: Run transform passes (drop insertion, panic scope wrap).
  ExitCode runTransforms();

  /// Stage 7: Lower to LLVM and emit output.
  ExitCode runCodeGen();

  /// Map string to EmitKind.
  static bool parseEmitKind(llvm::StringRef str, EmitKind &kind);

  /// Map string to OptLevel.
  static bool parseOptLevel(llvm::StringRef str, OptLevel &level);

  /// Print usage information.
  static void printUsage(llvm::raw_ostream &os);

  /// Print version information.
  static void printVersion(llvm::raw_ostream &os);

  DriverOptions opts;
  SourceManager sourceManager;
  std::unique_ptr<DiagnosticEngine> diags;

  /// The loaded source file ID.
  FileID sourceFileID;

  /// The MLIR context and module (created during HIR lowering).
  struct MLIRState;
  std::unique_ptr<MLIRState> mlirState;
};

} // namespace asc

#endif // ASC_DRIVER_DRIVER_H
