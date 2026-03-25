#include "asc/Driver/Driver.h"
#include "asc/AST/ASTContext.h"
#include "asc/HIR/HIRBuilder.h"
#include "asc/Lex/Lexer.h"
#include "asc/Parse/Parser.h"
#include "asc/Sema/Sema.h"
#include "asc/Analysis/LivenessAnalysis.h"
#include "asc/Analysis/RegionInference.h"
#include "asc/Analysis/AliasCheck.h"
#include "asc/Analysis/MoveCheck.h"
#include "asc/Analysis/SendSyncCheck.h"
#include "asc/Analysis/DropInsertion.h"
#include "asc/Analysis/PanicScopeWrap.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>

namespace asc {

/// Internal MLIR state held by the driver.
struct Driver::MLIRState {
  mlir::MLIRContext context;
  mlir::OwningOpRef<mlir::ModuleOp> module;
};

Driver::Driver() = default;
Driver::~Driver() = default;

ExitCode Driver::parseArgs(int argc, char **argv) {
  if (argc < 2) {
    printUsage(llvm::errs());
    return ExitCode::UsageError;
  }

  int i = 1;
  std::string subcmd = argv[i++];

  if (subcmd == "build")
    opts.subcommand = Subcommand::Build;
  else if (subcmd == "check")
    opts.subcommand = Subcommand::Check;
  else if (subcmd == "fmt")
    opts.subcommand = Subcommand::Fmt;
  else if (subcmd == "doc")
    opts.subcommand = Subcommand::Doc;
  else if (subcmd == "lsp")
    opts.subcommand = Subcommand::Lsp;
  else if (subcmd == "--help" || subcmd == "-h") {
    printUsage(llvm::outs());
    return ExitCode::Success;
  } else if (subcmd == "--version") {
    printVersion(llvm::outs());
    return ExitCode::Success;
  } else {
    // Treat as input file for implicit build.
    opts.subcommand = Subcommand::Build;
    opts.inputFile = subcmd;
  }

  while (i < argc) {
    llvm::StringRef arg(argv[i]);

    if (arg == "--target" && i + 1 < argc) {
      opts.targetTriple = argv[++i];
    } else if (arg == "--emit" && i + 1 < argc) {
      ++i;
      if (!parseEmitKind(argv[i], opts.emitKind)) {
        llvm::errs() << "error: unknown emit format '" << argv[i] << "'\n";
        return ExitCode::UsageError;
      }
    } else if (arg == "--opt" && i + 1 < argc) {
      ++i;
      if (!parseOptLevel(argv[i], opts.optLevel)) {
        llvm::errs() << "error: unknown optimization level '" << argv[i] << "'\n";
        return ExitCode::UsageError;
      }
    } else if (arg == "--debug") {
      opts.debugInfo = true;
    } else if (arg == "--verbose") {
      opts.verbose = true;
    } else if (arg == "--error-format" && i + 1 < argc) {
      ++i;
      llvm::StringRef fmt(argv[i]);
      if (fmt == "human")
        opts.errorFormat = ErrorFormat::Human;
      else if (fmt == "json")
        opts.errorFormat = ErrorFormat::JSON;
      else if (fmt == "github-actions")
        opts.errorFormat = ErrorFormat::GithubActions;
      else {
        llvm::errs() << "error: unknown error format '" << fmt << "'\n";
        return ExitCode::UsageError;
      }
    } else if (arg == "-o" && i + 1 < argc) {
      opts.outputFile = argv[++i];
    } else if (arg.starts_with("-")) {
      llvm::errs() << "error: unknown option '" << arg << "'\n";
      return ExitCode::UsageError;
    } else {
      opts.inputFile = arg.str();
    }
    ++i;
  }

  if (opts.inputFile.empty() && opts.subcommand != Subcommand::Lsp) {
    llvm::errs() << "error: no input file\n";
    return ExitCode::UsageError;
  }

  return ExitCode::Success;
}

ExitCode Driver::run() {
  switch (opts.subcommand) {
  case Subcommand::Build:
    return runBuild();
  case Subcommand::Check:
    return runCheck();
  case Subcommand::Fmt:
  case Subcommand::Doc:
  case Subcommand::Lsp:
    llvm::errs() << "error: subcommand not yet implemented\n";
    return ExitCode::SystemError;
  }
  return ExitCode::SystemError;
}

ExitCode Driver::runBuild() {
  ExitCode ec;

  auto start = std::chrono::steady_clock::now();

  if ((ec = loadSource()) != ExitCode::Success) return ec;
  if (opts.verbose)
    llvm::errs() << "  [1/7] loaded source\n";

  if ((ec = parseSource()) != ExitCode::Success) return ec;
  if (opts.verbose)
    llvm::errs() << "  [2/7] parsed\n";

  if ((ec = runSema()) != ExitCode::Success) return ec;
  if (opts.verbose)
    llvm::errs() << "  [3/7] semantic analysis\n";

  if ((ec = lowerToHIR()) != ExitCode::Success) return ec;
  if (opts.verbose)
    llvm::errs() << "  [4/7] HIR generated\n";

  if ((ec = runAnalysis()) != ExitCode::Success) return ec;
  if (opts.verbose)
    llvm::errs() << "  [5/7] borrow check passed\n";

  if ((ec = runTransforms()) != ExitCode::Success) return ec;
  if (opts.verbose)
    llvm::errs() << "  [6/7] transforms applied\n";

  if ((ec = runCodeGen()) != ExitCode::Success) return ec;
  if (opts.verbose) {
    auto end = std::chrono::steady_clock::now();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    llvm::errs() << "  [7/7] codegen complete (" << ms.count() << "ms)\n";
  }

  return ExitCode::Success;
}

ExitCode Driver::runCheck() {
  ExitCode ec;
  if ((ec = loadSource()) != ExitCode::Success) return ec;
  if ((ec = parseSource()) != ExitCode::Success) return ec;
  if ((ec = runSema()) != ExitCode::Success) return ec;
  if ((ec = lowerToHIR()) != ExitCode::Success) return ec;
  if ((ec = runAnalysis()) != ExitCode::Success) return ec;

  if (!diags->hasErrors())
    llvm::outs() << "check: no errors found\n";

  return diags->hasErrors() ? ExitCode::CompileError : ExitCode::Success;
}

ExitCode Driver::loadSource() {
  diags = std::make_unique<DiagnosticEngine>(sourceManager);
  diags->setErrorFormat(opts.errorFormat);

  sourceFileID = sourceManager.loadFile(opts.inputFile);
  if (!sourceFileID.isValid()) {
    llvm::errs() << "error: cannot open file '" << opts.inputFile << "'\n";
    return ExitCode::SystemError;
  }
  return ExitCode::Success;
}

// Stored between pipeline stages.
static std::unique_ptr<ASTContext> astCtx;
static std::vector<Decl *> topLevelDecls;
static std::unique_ptr<Sema> semaInstance;

ExitCode Driver::parseSource() {
  astCtx = std::make_unique<ASTContext>();
  Lexer lexer(sourceFileID, sourceManager, *diags);
  Parser parser(lexer, *astCtx, *diags);
  topLevelDecls = parser.parseProgram();

  if (diags->hasErrors())
    return ExitCode::CompileError;
  return ExitCode::Success;
}

ExitCode Driver::runSema() {
  semaInstance = std::make_unique<Sema>(*astCtx, *diags);
  semaInstance->analyze(topLevelDecls);

  if (diags->hasErrors())
    return ExitCode::CompileError;
  return ExitCode::Success;
}

ExitCode Driver::lowerToHIR() {
  mlirState = std::make_unique<MLIRState>();

  HIRBuilder hirBuilder(mlirState->context, *astCtx, *semaInstance);
  mlirState->module = hirBuilder.buildModule(topLevelDecls);

  if (!mlirState->module) {
    llvm::errs() << "error: HIR generation failed\n";
    return ExitCode::SystemError;
  }
  return ExitCode::Success;
}

ExitCode Driver::runAnalysis() {
  mlir::PassManager pm(&mlirState->context);

  // 5-pass borrow checker.
  pm.addNestedPass<mlir::func::FuncOp>(createLivenessAnalysisPass());
  pm.addNestedPass<mlir::func::FuncOp>(createRegionInferencePass());
  pm.addNestedPass<mlir::func::FuncOp>(createAliasCheckPass());
  pm.addNestedPass<mlir::func::FuncOp>(createMoveCheckPass());
  pm.addNestedPass<mlir::func::FuncOp>(createSendSyncCheckPass());

  if (failed(pm.run(*mlirState->module))) {
    return ExitCode::CompileError;
  }
  return ExitCode::Success;
}

ExitCode Driver::runTransforms() {
  mlir::PassManager pm(&mlirState->context);

  pm.addNestedPass<mlir::func::FuncOp>(createDropInsertionPass());
  pm.addNestedPass<mlir::func::FuncOp>(createPanicScopeWrapPass());

  if (failed(pm.run(*mlirState->module)))
    return ExitCode::SystemError;
  return ExitCode::Success;
}

ExitCode Driver::runCodeGen() {
  CodeGenOptions cgOpts;
  cgOpts.targetTriple = opts.targetTriple;
  cgOpts.emitKind = opts.emitKind;
  cgOpts.optLevel = opts.optLevel;
  cgOpts.debugInfo = opts.debugInfo;
  cgOpts.outputFile = opts.outputFile;

  // Default output file.
  if (cgOpts.outputFile.empty() && opts.emitKind == EmitKind::Wasm) {
    cgOpts.outputFile =
        opts.inputFile.substr(0, opts.inputFile.rfind('.')) + ".wasm";
  }

  CodeGenerator codegen(cgOpts);
  int result = codegen.generate(*mlirState->module);
  return result == 0 ? ExitCode::Success : ExitCode::SystemError;
}

bool Driver::parseEmitKind(llvm::StringRef str, EmitKind &kind) {
  if (str == "wasm") { kind = EmitKind::Wasm; return true; }
  if (str == "wat") { kind = EmitKind::Wat; return true; }
  if (str == "llvmir" || str == "llvm-ir") { kind = EmitKind::LLVMIR; return true; }
  if (str == "mlir") { kind = EmitKind::MLIR; return true; }
  if (str == "obj") { kind = EmitKind::Object; return true; }
  if (str == "asm") { kind = EmitKind::Asm; return true; }
  return false;
}

bool Driver::parseOptLevel(llvm::StringRef str, OptLevel &level) {
  if (str == "0") { level = OptLevel::O0; return true; }
  if (str == "1") { level = OptLevel::O1; return true; }
  if (str == "2") { level = OptLevel::O2; return true; }
  if (str == "3") { level = OptLevel::O3; return true; }
  if (str == "s") { level = OptLevel::Os; return true; }
  if (str == "z") { level = OptLevel::Oz; return true; }
  return false;
}

void Driver::printUsage(llvm::raw_ostream &os) {
  os << "Usage: asc <command> [options] <file>\n\n";
  os << "Commands:\n";
  os << "  build   Compile to output (default)\n";
  os << "  check   Frontend + borrow checker only\n";
  os << "  fmt     Format source\n";
  os << "  doc     Extract documentation\n";
  os << "  lsp     Start LSP server\n\n";
  os << "Options:\n";
  os << "  --target <triple>       Target triple (default: wasm32-wasi-threads)\n";
  os << "  --emit <format>         Output format: wasm|wat|llvmir|mlir|obj|asm\n";
  os << "  --opt <level>           Optimization: 0|1|2|3|s|z\n";
  os << "  --debug                 Emit debug info\n";
  os << "  --error-format <fmt>    human|json|github-actions\n";
  os << "  --verbose               Print pipeline timings\n";
  os << "  -o <file>               Output file\n";
}

void Driver::printVersion(llvm::raw_ostream &os) {
  os << "asc 0.1.0\n";
  os << "AssemblyScript compiler built on LLVM/MLIR\n";
}

} // namespace asc
