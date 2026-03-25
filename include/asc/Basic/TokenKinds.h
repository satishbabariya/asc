#ifndef ASC_BASIC_TOKENKINDS_H
#define ASC_BASIC_TOKENKINDS_H

#include "llvm/ADT/StringRef.h"

namespace asc {
namespace tok {

enum TokenKind : unsigned {
#define TOK(X) X,
#include "asc/Basic/TokenKinds.def"
  NUM_TOKENS
};

/// Get the spelling of a token kind, or nullptr for identifiers/literals.
const char *getTokenName(TokenKind kind);

/// Get the spelling string for a punctuator or keyword, or empty for others.
const char *getPunctuatorSpelling(TokenKind kind);

/// Get the keyword spelling, or empty if not a keyword.
const char *getKeywordSpelling(TokenKind kind);

/// Check if a token kind is a keyword.
bool isKeyword(TokenKind kind);

/// Look up a keyword from its spelling. Returns tok::identifier if not found.
TokenKind getKeywordTokenKind(llvm::StringRef spelling);

} // namespace tok
} // namespace asc

#endif // ASC_BASIC_TOKENKINDS_H
