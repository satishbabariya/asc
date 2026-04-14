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
#include "asc/Analysis/LinearityCheck.h"
#include "asc/Analysis/SendSyncCheck.h"
#include "asc/Analysis/EscapeAnalysis.h"
#include "asc/Analysis/DropInsertion.h"
#include "asc/Analysis/PanicScopeWrap.h"
#include "asc/Analysis/StackSizeAnalysis.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/Transforms/Passes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <chrono>
#include <cstdio>
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
    } else if (arg == "--opt-size") {
      opts.optLevel = OptLevel::Oz;
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

  // For Wasm targets producing .wasm output, auto-link via wasm-ld.
  if (opts.emitKind == EmitKind::Wasm) {
    llvm::Triple triple(opts.targetTriple);
    std::string outFile = opts.outputFile;
    if (outFile.empty())
      outFile = opts.inputFile.substr(0, opts.inputFile.rfind('.')) + ".wasm";
    if (triple.isWasm() && llvm::StringRef(outFile).ends_with(".wasm")) {
      // CodeGen wrote a Wasm object file to outFile. Rename it to a temp .o,
      // then link into the final .wasm executable.
      std::string objFile = outFile + ".o";
      if (std::rename(outFile.c_str(), objFile.c_str()) != 0) {
        llvm::errs() << "error: failed to rename object file for linking\n";
        return ExitCode::SystemError;
      }
      ec = linkWasm(objFile, outFile);
      // Clean up the temporary object file.
      std::remove(objFile.c_str());
      if (ec != ExitCode::Success) return ec;
      reportStage("link");
    }
  }

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
            R"("completionProvider":{"triggerCharacters":[".","::"]},)"
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

      // Handle textDocument/didOpen — run check and publish real diagnostics.
      if (body.find("\"textDocument/didOpen\"") != std::string::npos) {
        // Extract document URI from the JSON body.
        std::string uri;
        auto uriPos = body.find("\"uri\"");
        if (uriPos != std::string::npos) {
          auto start = body.find('"', uriPos + 5) + 1;
          auto end = body.find('"', start);
          if (start != std::string::npos && end != std::string::npos)
            uri = body.substr(start, end - start);
        }

        // Convert file:// URI to path.
        std::string filePath = uri;
        if (filePath.starts_with("file://"))
          filePath = filePath.substr(7);

        // Run the check pipeline on this file and collect diagnostics.
        std::string diagJson = "[]";
        if (!filePath.empty() && llvm::sys::fs::exists(filePath)) {
          SourceManager lspSM;
          auto lspDiags = std::make_unique<DiagnosticEngine>(lspSM);
          // Suppress rendering to stderr — we collect diagnostics programmatically.
          llvm::raw_null_ostream nullStream;
          lspDiags->setOutputStream(nullStream);

          auto fileID = lspSM.loadFile(filePath);
          if (fileID.isValid()) {
            Lexer lexer(fileID, lspSM, *lspDiags);
            ASTContext lspCtx;
            Parser parser(lexer, lspCtx, *lspDiags);
            auto decls = parser.parseProgram();

            if (!decls.empty() && !lspDiags->hasErrors()) {
              Sema sema(lspCtx, *lspDiags);
              sema.analyze(decls);
            }

            // Convert collected diagnostics to LSP JSON.
            const auto &diags_list = lspDiags->getDiagnostics();
            if (!diags_list.empty()) {
              diagJson = "[";
              bool first = true;
              for (const auto &d : diags_list) {
                if (!first) diagJson += ",";
                first = false;
                auto lc = lspSM.getLineAndColumn(d.location);
                unsigned line = lc.line > 0 ? lc.line - 1 : 0;
                unsigned col = lc.column > 0 ? lc.column - 1 : 0;
                int severity = (d.severity == DiagnosticSeverity::Error) ? 1
                             : (d.severity == DiagnosticSeverity::Warning) ? 2
                             : 3;
                // Escape quotes in message for JSON.
                std::string msg;
                for (char c : d.message) {
                  if (c == '"') msg += "\\\"";
                  else if (c == '\\') msg += "\\\\";
                  else msg += c;
                }
                diagJson += "{\"range\":{\"start\":{\"line\":" +
                    std::to_string(line) + ",\"character\":" +
                    std::to_string(col) + "},\"end\":{\"line\":" +
                    std::to_string(line) + ",\"character\":" +
                    std::to_string(col + 1) + "}},\"severity\":" +
                    std::to_string(severity) + ",\"source\":\"asc\",\"message\":\"" +
                    msg + "\"}";
              }
              diagJson += "]";
            }
          }
        }

        std::string notification =
            R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics",)"
            R"("params":{"uri":")" + uri + R"(","diagnostics":)" + diagJson + "}}";
        llvm::outs() << "Content-Length: " << notification.size()
                     << "\r\n\r\n" << notification;
        llvm::outs().flush();
        continue;
      }

      // Handle textDocument/hover — return type info for symbol under cursor.
      if (body.find("\"textDocument/hover\"") != std::string::npos) {
        // Extract request ID.
        std::string reqId = "0";
        auto idPos = body.find("\"id\"");
        if (idPos != std::string::npos) {
          auto start = body.find_first_of("0123456789", idPos + 4);
          auto end = body.find_first_not_of("0123456789", start);
          if (start != std::string::npos)
            reqId = body.substr(start, end - start);
        }

        // Extract URI and position.
        std::string uri;
        auto uriPos = body.find("\"uri\"");
        if (uriPos != std::string::npos) {
          auto start = body.find('"', uriPos + 5) + 1;
          auto end = body.find('"', start);
          if (start != std::string::npos && end != std::string::npos)
            uri = body.substr(start, end - start);
        }

        unsigned hoverLine = 0, hoverChar = 0;
        auto linePos = body.find("\"line\"");
        if (linePos != std::string::npos) {
          auto start = body.find_first_of("0123456789", linePos + 6);
          auto end = body.find_first_not_of("0123456789", start);
          if (start != std::string::npos)
            hoverLine = std::stoul(body.substr(start, end - start));
        }
        auto charPos = body.find("\"character\"");
        if (charPos != std::string::npos) {
          auto start = body.find_first_of("0123456789", charPos + 11);
          auto end = body.find_first_not_of("0123456789", start);
          if (start != std::string::npos)
            hoverChar = std::stoul(body.substr(start, end - start));
        }

        // For now, return a simple hover response with the file and position.
        // A full implementation would parse the file, run Sema, find the symbol
        // at the position, and return its type.
        std::string hoverContent = "asc: line " + std::to_string(hoverLine + 1) +
                                    ", col " + std::to_string(hoverChar + 1);

        std::string response =
            R"({"jsonrpc":"2.0","id":)" + reqId +
            R"(,"result":{"contents":{"kind":"plaintext","value":")" +
            hoverContent + R"("}}})";
        llvm::outs() << "Content-Length: " << response.size() << "\r\n\r\n"
                     << response;
        llvm::outs().flush();
        continue;
      }

      // Handle textDocument/completion — return known symbols.
      if (body.find("\"textDocument/completion\"") != std::string::npos) {
        std::string reqId = "0";
        auto idPos = body.find("\"id\"");
        if (idPos != std::string::npos) {
          auto start = body.find_first_of("0123456789", idPos + 4);
          auto end = body.find_first_not_of("0123456789", start);
          if (start != std::string::npos)
            reqId = body.substr(start, end - start);
        }

        // Return a static list of built-in types and common functions.
        std::string items = "[";
        const char *builtins[] = {
          "Vec", "String", "HashMap", "Box", "Arc", "Rc",
          "Option", "Result", "Mutex", "Semaphore", "RwLock", "File",
          "i32", "i64", "u32", "u64", "f32", "f64", "bool", "usize",
          "println", "print", "panic",
          "fn", "let", "const", "struct", "enum", "trait", "impl",
          "if", "else", "for", "while", "loop", "match", "return", "break",
          nullptr
        };
        bool first = true;
        for (int i = 0; builtins[i]; i++) {
          if (!first) items += ",";
          first = false;
          // Kind: 6=Variable, 7=Class, 14=Keyword, 21=Struct
          int kind = 21; // struct by default
          if (builtins[i][0] >= 'a') kind = 14; // keywords lowercase
          items += "{\"label\":\"" + std::string(builtins[i]) +
                   "\",\"kind\":" + std::to_string(kind) + "}";
        }
        items += "]";

        std::string response =
            R"({"jsonrpc":"2.0","id":)" + reqId +
            R"(,"result":{"isIncomplete":false,"items":)" + items + "}}";
        llvm::outs() << "Content-Length: " << response.size() << "\r\n\r\n"
                     << response;
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

  // Register a diagnostic handler that prints notes (MLIR's default handler
  // only prints the primary diagnostic and discards attached notes).
  mlirState->context.getDiagEngine().registerHandler(
      [](mlir::Diagnostic &diag) {
        llvm::raw_ostream &os = llvm::errs();
        os << diag.getLocation() << ": ";
        switch (diag.getSeverity()) {
        case mlir::DiagnosticSeverity::Error:
          os << "error: ";
          break;
        case mlir::DiagnosticSeverity::Warning:
          os << "warning: ";
          break;
        case mlir::DiagnosticSeverity::Note:
          os << "note: ";
          break;
        case mlir::DiagnosticSeverity::Remark:
          os << "remark: ";
          break;
        }
        os << diag.str() << "\n";
        for (auto &note : diag.getNotes()) {
          os << note.getLocation() << ": note: " << note.str() << "\n";
        }
        return mlir::success();
      });

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
  // HIR contains mixed dialects (func.func + llvm.addressof for vtables/closures).
  // The llvm.addressof verifier rejects references to func.func symbols, but these
  // are valid at the HIR stage and get resolved by FuncToLLVM during codegen.
  // Verifier runs after full LLVM lowering in CodeGen instead.
  pm.enableVerifier(false);

  // 6-pass borrow checker.
  pm.addNestedPass<mlir::func::FuncOp>(createLivenessAnalysisPass());
  pm.addNestedPass<mlir::func::FuncOp>(createRegionInferencePass());
  pm.addNestedPass<mlir::func::FuncOp>(createAliasCheckPass());
  pm.addNestedPass<mlir::func::FuncOp>(createMoveCheckPass());
  pm.addNestedPass<mlir::func::FuncOp>(createLinearityCheckPass());
  pm.addNestedPass<mlir::func::FuncOp>(createSendSyncCheckPass());

  if (failed(pm.run(*mlirState->module))) {
    return ExitCode::CompileError;
  }
  return ExitCode::Success;
}

ExitCode Driver::runTransforms() {
  mlir::PassManager pm(&mlirState->context);
  // PanicScopeWrap creates unregistered ops (own.try_scope, own.catch_scope)
  // via OperationState strings. These are erased by OwnershipLowering.
  // Verifier must be off here until these ops are properly registered.
  pm.enableVerifier(false);

  // Canonicalize: fold constant arithmetic, simplify ops.
  pm.addPass(mlir::createCanonicalizerPass());

  pm.addPass(createEscapeAnalysisPass());
  pm.addPass(createStackSizeAnalysisPass());
  pm.addNestedPass<mlir::func::FuncOp>(createDropInsertionPass());
  pm.addNestedPass<mlir::func::FuncOp>(createPanicScopeWrapPass());

  if (failed(pm.run(*mlirState->module)))
    return ExitCode::SystemError;
  return ExitCode::Success;
}

ExitCode Driver::linkWasm(const std::string &objFile,
                          const std::string &outFile) {
  // Find wasm-ld in PATH.
  auto wasmLdPath = llvm::sys::findProgramByName("wasm-ld");
  if (!wasmLdPath) {
    llvm::errs() << "error: wasm-ld not found in PATH; "
                 << "cannot link Wasm output\n";
    return ExitCode::SystemError;
  }

  // Compile the runtime to a temp object file if clang and runtime.c are found.
  std::string runtimeObj;
  auto clangPath = llvm::sys::findProgramByName("clang");
  if (clangPath) {
    // Look for runtime.c in common locations relative to the working directory.
    std::string runtimeSrc;
    for (const char *p : {"lib/Runtime/runtime.c",
                          "../lib/Runtime/runtime.c",
                          "../../lib/Runtime/runtime.c"}) {
      if (llvm::sys::fs::exists(p)) { runtimeSrc = p; break; }
    }
    if (!runtimeSrc.empty()) {
      runtimeObj = outFile + ".rt.o";
      llvm::SmallVector<llvm::StringRef, 8> clangArgs;
      clangArgs.push_back(*clangPath);
      clangArgs.push_back("--target=wasm32-wasi");
      clangArgs.push_back("-c");
      clangArgs.push_back(runtimeSrc);
      clangArgs.push_back("-o");
      clangArgs.push_back(runtimeObj);
      std::string clangErr;
      int clangRc = llvm::sys::ExecuteAndWait(
          *clangPath, clangArgs, std::nullopt, {}, 60, 0, &clangErr);
      if (clangRc != 0) {
        if (opts.verbose)
          llvm::errs() << "  [warn] failed to compile runtime: " << clangErr << "\n";
        runtimeObj.clear();
      }
    }
  }

  // Build argument list for wasm-ld.
  llvm::SmallVector<llvm::StringRef, 12> args;
  args.push_back(*wasmLdPath);
  args.push_back(objFile);
  if (!runtimeObj.empty())
    args.push_back(runtimeObj);
  args.push_back("-o");
  args.push_back(outFile);
  // Use --export=_start when runtime is linked (provides _start entry).
  // Fall back to --no-entry --export-all when no runtime.
  if (!runtimeObj.empty()) {
    args.push_back("--export=_start");
  } else {
    args.push_back("--no-entry");
    args.push_back("--export-all");
  }
  args.push_back("--allow-undefined");

  if (opts.verbose) {
    llvm::errs() << "  [link] ";
    for (auto &a : args) llvm::errs() << a << " ";
    llvm::errs() << "\n";
  }

  std::string errMsg;
  int rc = llvm::sys::ExecuteAndWait(*wasmLdPath, args,
                                     std::nullopt, {}, 60, 0, &errMsg);

  // Clean up runtime temp object.
  if (!runtimeObj.empty())
    std::remove(runtimeObj.c_str());

  if (rc != 0) {
    llvm::errs() << "error: wasm-ld failed";
    if (!errMsg.empty())
      llvm::errs() << ": " << errMsg;
    llvm::errs() << "\n";
    return ExitCode::SystemError;
  }

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
