#include "asc/Basic/TokenKinds.h"
#include "llvm/ADT/StringSwitch.h"

namespace asc {
namespace tok {

const char *getTokenName(TokenKind kind) {
  switch (kind) {
#define TOK(X) case X: return #X;
#include "asc/Basic/TokenKinds.def"
  default: return "unknown";
  }
}

const char *getPunctuatorSpelling(TokenKind kind) {
  switch (kind) {
#define PUNCTUATOR(X, S) case X: return S;
#include "asc/Basic/TokenKinds.def"
  default: return "";
  }
}

const char *getKeywordSpelling(TokenKind kind) {
  switch (kind) {
#define KEYWORD(X, S) case kw_##X: return S;
#include "asc/Basic/TokenKinds.def"
  default: return "";
  }
}

bool isKeyword(TokenKind kind) {
  switch (kind) {
#define KEYWORD(X, S) case kw_##X: return true;
#include "asc/Basic/TokenKinds.def"
  default: return false;
  }
}

TokenKind getKeywordTokenKind(llvm::StringRef spelling) {
  return llvm::StringSwitch<TokenKind>(spelling)
#define KEYWORD(X, S) .Case(S, kw_##X)
#include "asc/Basic/TokenKinds.def"
      .Default(identifier);
}

} // namespace tok
} // namespace asc
