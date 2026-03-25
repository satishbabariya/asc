#include "asc/Basic/Diagnostic.h"
#include "asc/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

namespace asc {

// --- DiagBuilder ---

DiagnosticEngine::DiagBuilder::DiagBuilder(DiagnosticEngine &engine,
                                           Diagnostic diag)
    : engine(engine), diag(std::move(diag)) {}

DiagnosticEngine::DiagBuilder::~DiagBuilder() {
  if (!emitted)
    engine.emit(std::move(diag));
}

DiagnosticEngine::DiagBuilder &
DiagnosticEngine::DiagBuilder::addNote(SourceLocation loc,
                                       llvm::StringRef msg) {
  diag.notes.push_back({loc, msg.str()});
  return *this;
}

DiagnosticEngine::DiagBuilder &
DiagnosticEngine::DiagBuilder::addFixIt(SourceRange range,
                                        llvm::StringRef replacement) {
  diag.fixIts.push_back({range, replacement.str()});
  return *this;
}

// --- DiagnosticEngine ---

DiagnosticEngine::DiagnosticEngine(const SourceManager &sm)
    : sm(sm), out(&llvm::errs()) {}

DiagnosticEngine::DiagBuilder
DiagnosticEngine::report(SourceLocation loc, DiagID id,
                         llvm::StringRef message) {
  Diagnostic diag;
  diag.severity = getDefaultSeverity(id);
  diag.id = id;
  diag.location = loc;
  diag.message = message.str();
  return DiagBuilder(*this, std::move(diag));
}

void DiagnosticEngine::emitError(SourceLocation loc, DiagID id,
                                 llvm::StringRef message) {
  Diagnostic diag;
  diag.severity = DiagnosticSeverity::Error;
  diag.id = id;
  diag.location = loc;
  diag.message = message.str();
  emit(std::move(diag));
}

void DiagnosticEngine::emitWarning(SourceLocation loc, DiagID id,
                                   llvm::StringRef message) {
  Diagnostic diag;
  diag.severity = DiagnosticSeverity::Warning;
  diag.id = id;
  diag.location = loc;
  diag.message = message.str();
  emit(std::move(diag));
}

void DiagnosticEngine::emit(Diagnostic diag) {
  if (diag.severity == DiagnosticSeverity::Error ||
      diag.severity == DiagnosticSeverity::ICE)
    ++errorCount;
  else if (diag.severity == DiagnosticSeverity::Warning)
    ++warningCount;

  diagnostics.push_back(diag);

  switch (format) {
  case ErrorFormat::Human:
    renderHuman(diag);
    break;
  case ErrorFormat::JSON:
    renderJSON(diag);
    break;
  case ErrorFormat::GithubActions:
    renderGithubActions(diag);
    break;
  }
}

static const char *severityToString(DiagnosticSeverity sev) {
  switch (sev) {
  case DiagnosticSeverity::Note:    return "note";
  case DiagnosticSeverity::Warning: return "warning";
  case DiagnosticSeverity::Error:   return "error";
  case DiagnosticSeverity::ICE:     return "internal error";
  }
  return "unknown";
}

void DiagnosticEngine::renderHuman(const Diagnostic &diag) {
  auto &os = *out;
  const char *code = getDiagCode(diag.id);

  // Severity + code + message
  if (code)
    os << severityToString(diag.severity) << "[" << code << "]: ";
  else
    os << severityToString(diag.severity) << ": ";
  os << diag.message << "\n";

  // Source location + excerpt
  if (diag.location.isValid()) {
    auto lc = sm.getLineAndColumn(diag.location);
    llvm::StringRef filename = sm.getFilename(diag.location.getFileID());
    os << "  --> " << filename << ":" << lc.line << ":" << lc.column << "\n";

    llvm::StringRef line = sm.getSourceLine(diag.location);
    os << "   | \n";
    os << "   | " << line << "\n";
    os << "   | ";
    for (unsigned i = 1; i < lc.column; ++i)
      os << " ";
    os << "^\n";
  }

  // Notes
  for (const auto &note : diag.notes) {
    os << "  = note: " << note.message;
    if (note.location.isValid()) {
      auto nlc = sm.getLineAndColumn(note.location);
      llvm::StringRef nfile = sm.getFilename(note.location.getFileID());
      os << " --> " << nfile << ":" << nlc.line << ":" << nlc.column;
    }
    os << "\n";
  }

  // Fix-its
  for (const auto &fixit : diag.fixIts) {
    os << "  = suggestion: replace with `" << fixit.replacement << "`\n";
  }
}

void DiagnosticEngine::renderJSON(const Diagnostic &diag) {
  auto &os = *out;
  os << "{";
  os << "\"severity\":\"" << severityToString(diag.severity) << "\"";
  const char *code = getDiagCode(diag.id);
  if (code)
    os << ",\"code\":\"" << code << "\"";
  os << ",\"message\":\"";
  // Simple JSON string escaping
  for (char c : diag.message) {
    if (c == '"')
      os << "\\\"";
    else if (c == '\\')
      os << "\\\\";
    else if (c == '\n')
      os << "\\n";
    else
      os << c;
  }
  os << "\"";
  if (diag.location.isValid()) {
    auto lc = sm.getLineAndColumn(diag.location);
    llvm::StringRef filename = sm.getFilename(diag.location.getFileID());
    os << ",\"file\":\"" << filename << "\"";
    os << ",\"line\":" << lc.line;
    os << ",\"column\":" << lc.column;
  }
  os << "}\n";
}

void DiagnosticEngine::renderGithubActions(const Diagnostic &diag) {
  auto &os = *out;
  if (diag.severity == DiagnosticSeverity::Error ||
      diag.severity == DiagnosticSeverity::ICE) {
    os << "::error";
  } else if (diag.severity == DiagnosticSeverity::Warning) {
    os << "::warning";
  } else {
    os << "::notice";
  }
  if (diag.location.isValid()) {
    auto lc = sm.getLineAndColumn(diag.location);
    llvm::StringRef filename = sm.getFilename(diag.location.getFileID());
    os << " file=" << filename << ",line=" << lc.line << ",col=" << lc.column;
  }
  os << "::" << diag.message << "\n";
}

} // namespace asc
