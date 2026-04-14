#ifndef ASC_BASIC_DIAGNOSTICIDS_H
#define ASC_BASIC_DIAGNOSTICIDS_H

namespace asc {

/// Diagnostic severity levels.
enum class DiagnosticSeverity {
  Note,
  Warning,
  Error,
  ICE, // Internal Compiler Error
};

/// Diagnostic IDs from RFC-0010.
enum class DiagID : unsigned {
  // Errors
  ErrMutableBorrowWhileShared = 1,    // E001
  ErrSharedBorrowWhileMutable = 2,    // E002
  ErrBorrowOutlivesOwner = 3,         // E003
  ErrUseAfterMove = 4,                // E004
  ErrMoveInConditionalBranch = 5,     // E005
  ErrNonSendCaptured = 6,             // E006
  ErrMissingCopyAttribute = 7,        // E007
  ErrNonSendChannelElement = 8,       // E008
  ErrUnboundedRecursion = 9,          // E009
  ErrDoublePanic = 10,                // E010

  // Ownership linearity
  ErrResourceLeak = 11,               // E011
  ErrDoubleConsume = 12,              // E012

  // Warnings
  WarnConditionalMoveDropFlag = 101,  // W001
  WarnLargeCopyType = 102,            // W002
  WarnNonExhaustiveMatch = 103,       // W003
  WarnResourceLeak = 104,            // W004
  WarnUnusedVariable = 105,           // W005

  // General compiler diagnostics
  ErrUnexpectedToken = 200,
  ErrExpectedExpression = 201,
  ErrExpectedType = 202,
  ErrExpectedSemicolon = 203,
  ErrExpectedClosingBrace = 204,
  ErrExpectedClosingParen = 205,
  ErrExpectedIdentifier = 206,
  ErrUndeclaredIdentifier = 207,
  ErrTypeMismatch = 208,
  ErrDuplicateDeclaration = 209,
  ErrInvalidLiteral = 210,
  ErrUnsupportedFeature = 211,
  ErrTraitNotImplemented = 212,
  ErrGenericArgCountMismatch = 213,
  ErrInvalidBreak = 214,
  ErrInvalidContinue = 215,
  ErrMissingReturnValue = 216,
  ErrInvalidAssignTarget = 217,
  ErrUnterminatedString = 218,
  ErrUnterminatedComment = 219,
  ErrInvalidEscape = 220,
  ErrExpectedClosingBracket = 221,

  // Notes
  NoteDefinedHere = 300,
  NotePreviousBorrow = 301,
  NoteMovedHere = 302,
  NoteSuggestion = 303,
};

/// Get the default severity for a diagnostic ID.
inline DiagnosticSeverity getDefaultSeverity(DiagID id) {
  unsigned raw = static_cast<unsigned>(id);
  if (raw >= 101 && raw <= 199)
    return DiagnosticSeverity::Warning;
  if (raw >= 300)
    return DiagnosticSeverity::Note;
  return DiagnosticSeverity::Error;
}

/// Get the error code string (e.g., "E001", "W001") for a diagnostic ID.
inline const char *getDiagCode(DiagID id) {
  switch (id) {
  case DiagID::ErrMutableBorrowWhileShared: return "E001";
  case DiagID::ErrSharedBorrowWhileMutable: return "E002";
  case DiagID::ErrBorrowOutlivesOwner: return "E003";
  case DiagID::ErrUseAfterMove: return "E004";
  case DiagID::ErrMoveInConditionalBranch: return "E005";
  case DiagID::ErrNonSendCaptured: return "E006";
  case DiagID::ErrMissingCopyAttribute: return "E007";
  case DiagID::ErrNonSendChannelElement: return "E008";
  case DiagID::ErrUnboundedRecursion: return "E009";
  case DiagID::ErrDoublePanic: return "E010";
  case DiagID::WarnConditionalMoveDropFlag: return "W001";
  case DiagID::WarnLargeCopyType: return "W002";
  case DiagID::WarnNonExhaustiveMatch: return "W003";
  case DiagID::WarnResourceLeak: return "W004";
  case DiagID::WarnUnusedVariable: return "W005";
  case DiagID::ErrResourceLeak: return "E011";
  case DiagID::ErrDoubleConsume: return "E012";
  default: return nullptr;
  }
}

} // namespace asc

#endif // ASC_BASIC_DIAGNOSTICIDS_H
