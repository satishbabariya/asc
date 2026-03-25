#ifndef ASC_LEX_LEXER_H
#define ASC_LEX_LEXER_H

#include "asc/Lex/Token.h"
#include "asc/Basic/Diagnostic.h"
#include "asc/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"

namespace asc {

class Lexer {
public:
  Lexer(FileID fileID, const SourceManager &sm, DiagnosticEngine &diags);

  /// Lex the next token.
  Token lex();

  /// Peek at the next token without consuming it.
  const Token &peek();

  /// Check if we've reached end of file.
  bool isEOF() const;

private:
  void lexImpl(Token &result);
  void lexIdentifierOrKeyword(Token &result);
  void lexNumericLiteral(Token &result);
  void lexStringLiteral(Token &result);
  void lexCharLiteral(Token &result);
  void lexTemplateLiteral(Token &result);
  void lexRawStringLiteral(Token &result);
  void skipLineComment();
  void skipBlockComment();
  void lexDocLineComment(Token &result);
  void lexDocBlockComment(Token &result);
  void lexAttribute(Token &result);
  bool lexEscapeSequence(std::string &out);

  char peekChar() const;
  char peekChar(unsigned offset) const;
  char advanceChar();
  SourceLocation currentLoc() const;

  FileID fileID;
  const SourceManager &sm;
  DiagnosticEngine &diags;
  llvm::StringRef buffer;
  unsigned curOffset = 0;

  // For peek support
  bool hasPeeked = false;
  Token peekedToken;

  // Template literal nesting depth
  unsigned templateDepth = 0;
};

} // namespace asc

#endif // ASC_LEX_LEXER_H
