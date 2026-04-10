#include "asc/Driver/Driver.h"
#include "asc/AST/ASTContext.h"
#include "asc/HIR/HIRBuilder.h"
#include "asc/Lex/Lexer.h"
#include "asc/Parse/Parser.h"
#include "asc/Sema/Sema.h"
#include "asc/AST/Decl.h"
#include "asc/Analysis/LivenessAnalysis.h"
#include "asc/Analysis/RegionInference.h"
#include "asc/Analysis/AliasCheck.h"
#include "asc/Analysis/MoveCheck.h"
#include "asc/Analysis/SendSyncCheck.h"
#include "asc/Analysis/DropInsertion.h"
#include "asc/Analysis/PanicScopeWrap.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <iostream>

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
    return runFmt();
  case Subcommand::Doc:
    return runDoc();
  case Subcommand::Lsp:
    return runLsp();
  }
  return ExitCode::SystemError;
}

ExitCode Driver::runBuild() {
  ExitCode ec;

  auto start = std::chrono::steady_clock::now();
  auto stageStart = start;

  auto reportStage = [&](const char *name) {
    if (!opts.verbose) return;
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        now - stageStart);
    llvm::errs() << "  [" << name << "] " << (ms.count() / 1000.0) << "ms\n";
    stageStart = now;
  };

  if ((ec = loadSource()) != ExitCode::Success) return ec;
  reportStage("load");

  if ((ec = parseSource()) != ExitCode::Success) return ec;
  reportStage("parse");

  if ((ec = runSema()) != ExitCode::Success) return ec;
  reportStage("sema");

  if ((ec = lowerToHIR()) != ExitCode::Success) return ec;
  reportStage("hir");

  if ((ec = runAnalysis()) != ExitCode::Success) return ec;
  reportStage("check");

  if ((ec = runTransforms()) != ExitCode::Success) return ec;
  reportStage("transform");

  // If --emit mlir, dump MLIR before lowering and stop.
  if (opts.emitKind == EmitKind::MLIR) {
    if (opts.outputFile.empty()) {
      mlirState->module->print(llvm::outs());
    } else {
      std::error_code fileEc;
      llvm::raw_fd_ostream os(opts.outputFile, fileEc);
      if (fileEc) {
        llvm::errs() << "error: " << fileEc.message() << "\n";
        return ExitCode::SystemError;
      }
      mlirState->module->print(os);
    }
    return ExitCode::Success;
  }

  if ((ec = runCodeGen()) != ExitCode::Success) return ec;
  reportStage("codegen");

  if (opts.verbose) {
    auto end = std::chrono::steady_clock::now();
    auto totalMs =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    llvm::errs() << "  [total] " << (totalMs.count() / 1000.0) << "ms\n";
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

ExitCode Driver::runFmt() {
  // Load the source file.
  diags = std::make_unique<DiagnosticEngine>(sourceManager);
  sourceFileID = sourceManager.loadFile(opts.inputFile);
  if (!sourceFileID.isValid()) {
    llvm::errs() << "error: cannot open file '" << opts.inputFile << "'\n";
    return ExitCode::SystemError;
  }

  // Re-lex into token stream.
  Lexer lexer(sourceFileID, sourceManager, *diags);
  std::string formatted;
  llvm::raw_string_ostream out(formatted);

  unsigned indent = 0;
  bool needNewline = false;
  bool lastWasNewline = true;
  bool lastWasOpen = false; // after ( or [

  while (true) {
    Token tok = lexer.lex();
    if (tok.is(tok::eof))
      break;

    // Skip doc comments — re-emit them directly.
    if (tok.is(tok::doc_line_comment) || tok.is(tok::doc_block_comment)) {
      if (!lastWasNewline) out << "\n";
      for (unsigned i = 0; i < indent; ++i) out << "  ";
      out << tok.getSpelling() << "\n";
      lastWasNewline = true;
      continue;
    }

    // Handle closing braces — dedent before emitting.
    if (tok.is(tok::r_brace)) {
      if (indent > 0) --indent;
      if (!lastWasNewline) out << "\n";
      for (unsigned i = 0; i < indent; ++i) out << "  ";
      out << "}";
      needNewline = true;
      lastWasNewline = false;
      continue;
    }

    // If we need a newline (after ; or { or top-level decl keyword).
    if (needNewline) {
      out << "\n";
      needNewline = false;
      lastWasNewline = true;
    }

    // Indent at start of line.
    bool atLineStart = lastWasNewline;
    if (lastWasNewline) {
      for (unsigned i = 0; i < indent; ++i) out << "  ";
      lastWasNewline = false;
    }

    // Emit the token with smart spacing.
    // No space: after ( [, before ) ] , ; : . (, at line start.
    if (!atLineStart && !lastWasOpen &&
        !tok.isOneOf(tok::comma, tok::semicolon, tok::colon, tok::dot,
                     tok::r_paren, tok::r_bracket, tok::l_paren, tok::l_bracket)) {
      out << " ";
    }

    out << tok.getSpelling();
    lastWasOpen = tok.isOneOf(tok::l_paren, tok::l_bracket);

    // Handle opening braces — indent after.
    if (tok.is(tok::l_brace)) {
      ++indent;
      needNewline = true;
    }

    if (tok.is(tok::semicolon))
      needNewline = true;
  }

  if (!lastWasNewline)
    out << "\n";

  // Write to file or stdout.
  if (opts.outputFile.empty()) {
    // In-place: write back to input file.
    std::error_code ec;
    llvm::raw_fd_ostream fileOut(opts.inputFile, ec);
    if (ec) {
      llvm::errs() << "error: " << ec.message() << "\n";
      return ExitCode::SystemError;
    }
    fileOut << formatted;
  } else {
    llvm::outs() << formatted;
  }

  return ExitCode::Success;
}

ExitCode Driver::runDoc() {
  // Load and parse the source file.
  diags = std::make_unique<DiagnosticEngine>(sourceManager);
  sourceFileID = sourceManager.loadFile(opts.inputFile);
  if (!sourceFileID.isValid()) {
    llvm::errs() << "error: cannot open file '" << opts.inputFile << "'\n";
    return ExitCode::SystemError;
  }

  Lexer lexer(sourceFileID, sourceManager, *diags);

  // Collect doc comments and their associated tokens.
  llvm::raw_ostream &out = llvm::outs();
  out << "# Module: " << opts.inputFile << "\n\n";

  std::string pendingDoc;
  while (true) {
    Token tok = lexer.lex();
    if (tok.is(tok::eof)) break;

    // Collect doc comments.
    if (tok.is(tok::doc_line_comment)) {
      llvm::StringRef comment = tok.getSpelling();
      if (comment.starts_with("///"))
        comment = comment.drop_front(3).ltrim();
      pendingDoc += comment.str();
      pendingDoc += "\n";
      continue;
    }
    if (tok.is(tok::doc_block_comment)) {
      llvm::StringRef comment = tok.getSpelling();
      if (comment.starts_with("/**") && comment.ends_with("*/"))
        comment = comment.drop_front(3).drop_back(2).trim();
      pendingDoc += comment.str();
      pendingDoc += "\n";
      continue;
    }

    // If we hit a declaration keyword after doc comment, emit doc entry.
    if (!pendingDoc.empty()) {
      if (tok.is(tok::kw_function) || tok.is(tok::kw_fn)) {
        Token name = lexer.lex();
        out << "### `function " << name.getSpelling() << "(...)`\n\n";
        out << pendingDoc << "\n";
      } else if (tok.is(tok::kw_struct)) {
        Token name = lexer.lex();
        out << "### `struct " << name.getSpelling() << "`\n\n";
        out << pendingDoc << "\n";
      } else if (tok.is(tok::kw_enum)) {
        Token name = lexer.lex();
        out << "### `enum " << name.getSpelling() << "`\n\n";
        out << pendingDoc << "\n";
      } else if (tok.is(tok::kw_trait)) {
        Token name = lexer.lex();
        out << "### `trait " << name.getSpelling() << "`\n\n";
        out << pendingDoc << "\n";
      }
      pendingDoc.clear();
    }
  }

  return ExitCode::Success;
}

ExitCode Driver::runLsp() {
  // Minimal LSP server: read JSON-RPC from stdin, respond on stdout.
  // Supports: initialize, textDocument/didOpen (with diagnostics).
  llvm::errs() << "asc LSP server starting...\n";

  std::string line;
  while (std::getline(std::cin, line)) {
    // Read Content-Length header.
    if (line.starts_with("Content-Length:")) {
      unsigned len = 0;
      llvm::StringRef(line).drop_front(15).trim().getAsInteger(10, len);
      std::getline(std::cin, line); // empty line
      std::string body(len, '\0');
      std::cin.read(body.data(), len);

      // Handle initialize request.
      if (body.find("\"initialize\"") != std::string::npos) {
        std::string response =
            R"({"jsonrpc":"2.0","id":0,"result":{"capabilities":{)"
            R"("textDocumentSync":1,)"
            R"("hoverProvider":true,)"
            R"("diagnosticProvider":{"interFileDependencies":false,"workspaceDiagnostics":false})"
            R"(},"serverInfo":{"name":"asc","version":"0.1.0"}}})";
        llvm::outs() << "Content-Length: " << response.size() << "\r\n\r\n"
                     << response;
        llvm::outs().flush();
        continue;
      }

      // Handle shutdown.
      if (body.find("\"shutdown\"") != std::string::npos) {
        std::string response =
            R"({"jsonrpc":"2.0","id":1,"result":null})";
        llvm::outs() << "Content-Length: " << response.size() << "\r\n\r\n"
                     << response;
        llvm::outs().flush();
        continue;
      }

      // Handle exit.
      if (body.find("\"exit\"") != std::string::npos) {
        return ExitCode::Success;
      }

      // Handle textDocument/didOpen — run check and publish diagnostics.
      if (body.find("\"textDocument/didOpen\"") != std::string::npos) {
        // DECISION: For now, publish empty diagnostics.
        // Full integration would parse the document URI, run sema,
        // and return real diagnostics.
        std::string notification =
            R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics",)"
            R"("params":{"uri":"","diagnostics":[]}})";
        llvm::outs() << "Content-Length: " << notification.size()
                     << "\r\n\r\n" << notification;
        llvm::outs().flush();
        continue;
      }
    }
  }

  return ExitCode::Success;
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

  // Resolve imports: for each ImportDecl, parse the imported module
  // and merge its exported declarations into our declaration list.
  resolveImports();

  return ExitCode::Success;
}

void Driver::resolveImports() {
  // Collect all ImportDecls from top-level declarations.
  llvm::StringSet<> processedModules;
  llvm::SmallVector<ImportDecl *, 4> imports;
  for (auto *decl : topLevelDecls) {
    if (auto *id = dynamic_cast<ImportDecl *>(decl))
      imports.push_back(id);
  }

  for (auto *imp : imports) {
    std::string modulePath = imp->getModulePath().str();
    if (processedModules.count(modulePath))
      continue;
    processedModules.insert(modulePath);

    // Resolve path relative to input file directory.
    std::string dir;
    auto lastSlash = opts.inputFile.rfind('/');
    if (lastSlash != std::string::npos)
      dir = opts.inputFile.substr(0, lastSlash + 1);

    std::string resolvedPath = dir + modulePath;
    if (!resolvedPath.ends_with(".ts"))
      resolvedPath += ".ts";

    // Try to load and parse the imported file.
    FileID importFid = sourceManager.loadFile(resolvedPath);
    if (!importFid.isValid()) {
      llvm::errs() << "warning: cannot resolve import '" << modulePath
                    << "' (file: " << resolvedPath << ")\n";
      continue;
    }

    Lexer importLexer(importFid, sourceManager, *diags);
    Parser importParser(importLexer, *astCtx, *diags);
    auto importedDecls = importParser.parseProgram();

    // Merge exported declarations into our top-level list.
    for (auto *decl : importedDecls) {
      if (auto *ed = dynamic_cast<ExportDecl *>(decl)) {
        if (ed->getInner())
          topLevelDecls.push_back(ed->getInner());
      }
      // Also include non-exported functions/structs that might be
      // referenced by the imported symbols.
      if (dynamic_cast<FunctionDecl *>(decl) ||
          dynamic_cast<StructDecl *>(decl) ||
          dynamic_cast<EnumDecl *>(decl))
        topLevelDecls.push_back(decl);
    }
  }
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

  HIRBuilder hirBuilder(mlirState->context, *astCtx, *semaInstance,
                        sourceManager);
  mlirState->module = hirBuilder.buildModule(topLevelDecls);

  if (!mlirState->module) {
    llvm::errs() << "error: HIR generation failed\n";
    return ExitCode::SystemError;
  }
  return ExitCode::Success;
}

ExitCode Driver::runAnalysis() {
  mlir::PassManager pm(&mlirState->context);
  // Enable MLIR verification between passes to catch IR issues early.
  pm.enableVerifier(true);

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
  pm.enableVerifier(true);

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
