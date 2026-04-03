#ifndef ASC_LEX_TOKEN_H
#define ASC_LEX_TOKEN_H

#include "asc/Basic/TokenKinds.h"
#include "asc/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <cstdint>

namespace asc {

class Token {
public:
  Token() : kind(tok::unknown) {}
  Token(tok::TokenKind kind, SourceLocation loc, llvm::StringRef spelling)
      : kind(kind), loc(loc), spelling(spelling.str()) {}

  tok::TokenKind getKind() const { return kind; }
  SourceLocation getLocation() const { return loc; }
  llvm::StringRef getSpelling() const { return spelling; }

  bool is(tok::TokenKind k) const { return kind == k; }
  bool isNot(tok::TokenKind k) const { return kind != k; }
  bool isOneOf(tok::TokenKind k1, tok::TokenKind k2) const {
    return is(k1) || is(k2);
  }
  template <typename... Ts>
  bool isOneOf(tok::TokenKind k1, tok::TokenKind k2, Ts... ks) const {
    return is(k1) || isOneOf(k2, ks...);
  }

  bool isKeyword() const { return tok::isKeyword(kind); }
  bool isLiteral() const {
    return kind == tok::integer_literal || kind == tok::float_literal ||
           kind == tok::string_literal || kind == tok::char_literal;
  }

  // For integer literals
  void setIntegerValue(uint64_t val) { intVal = val; }
  uint64_t getIntegerValue() const { return intVal; }

  // For float literals
  void setFloatValue(double val) { floatVal = val; }
  double getFloatValue() const { return floatVal; }

  // For integer/float suffix type
  void setSuffixType(llvm::StringRef suffix) { suffixType = suffix.str(); }
  llvm::StringRef getSuffixType() const { return suffixType; }

private:
  tok::TokenKind kind;
  SourceLocation loc;
  std::string spelling;
  std::string suffixType;
  union {
    uint64_t intVal = 0;
    double floatVal;
  };
};

} // namespace asc

#endif // ASC_LEX_TOKEN_H
