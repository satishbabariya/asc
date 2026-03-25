#ifndef ASC_BASIC_DIAGNOSTIC_H
#define ASC_BASIC_DIAGNOSTIC_H

#include "asc/Basic/DiagnosticIDs.h"
#include "asc/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <string>

namespace asc {

class SourceManager;

/// A single emitted diagnostic with location, message, and optional notes.
struct Diagnostic {
  DiagnosticSeverity severity;
  DiagID id;
  SourceLocation location;
  std::string message;

  struct Note {
    SourceLocation location;
    std::string message;
  };
  llvm::SmallVector<Note, 2> notes;

  struct FixIt {
    SourceRange range;
    std::string replacement;
  };
  llvm::SmallVector<FixIt, 1> fixIts;
};

/// Error output format.
enum class ErrorFormat {
  Human,
  JSON,
  GithubActions,
};

/// Collects and renders compiler diagnostics.
class DiagnosticEngine {
public:
  explicit DiagnosticEngine(const SourceManager &sm);

  /// Set the output format.
  void setErrorFormat(ErrorFormat fmt) { format = fmt; }

  /// Set output stream (defaults to llvm::errs()).
  void setOutputStream(llvm::raw_ostream &os) { out = &os; }

  /// Report a diagnostic. Returns a builder for adding notes/fixits.
  class DiagBuilder {
  public:
    DiagBuilder &addNote(SourceLocation loc, llvm::StringRef msg);
    DiagBuilder &addFixIt(SourceRange range, llvm::StringRef replacement);
    ~DiagBuilder();

  private:
    friend class DiagnosticEngine;
    DiagBuilder(DiagnosticEngine &engine, Diagnostic diag);
    DiagnosticEngine &engine;
    Diagnostic diag;
    bool emitted = false;
  };

  DiagBuilder report(SourceLocation loc, DiagID id, llvm::StringRef message);

  /// Shorthand: report error without builder.
  void emitError(SourceLocation loc, DiagID id, llvm::StringRef message);

  /// Shorthand: report warning.
  void emitWarning(SourceLocation loc, DiagID id, llvm::StringRef message);

  /// Check if any errors were emitted.
  bool hasErrors() const { return errorCount > 0; }

  /// Get total error count.
  unsigned getErrorCount() const { return errorCount; }

  /// Get total warning count.
  unsigned getWarningCount() const { return warningCount; }

  /// Get all emitted diagnostics (for testing).
  const llvm::SmallVector<Diagnostic, 8> &getDiagnostics() const {
    return diagnostics;
  }

private:
  void emit(Diagnostic diag);
  void renderHuman(const Diagnostic &diag);
  void renderJSON(const Diagnostic &diag);
  void renderGithubActions(const Diagnostic &diag);

  const SourceManager &sm;
  llvm::raw_ostream *out;
  ErrorFormat format = ErrorFormat::Human;
  unsigned errorCount = 0;
  unsigned warningCount = 0;
  llvm::SmallVector<Diagnostic, 8> diagnostics;
};

} // namespace asc

#endif // ASC_BASIC_DIAGNOSTIC_H
