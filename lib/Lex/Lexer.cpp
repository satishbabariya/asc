#include "asc/Lex/Lexer.h"
#include "asc/Basic/DiagnosticIDs.h"
#include "llvm/ADT/StringSwitch.h"
#include <cctype>
#include <charconv>

namespace asc {

Lexer::Lexer(FileID fileID, const SourceManager &sm, DiagnosticEngine &diags)
    : fileID(fileID), sm(sm), diags(diags) {
  buffer = sm.getBufferData(fileID);
}

char Lexer::peekChar() const {
  if (curOffset >= buffer.size())
    return '\0';
  return buffer[curOffset];
}

char Lexer::peekChar(unsigned offset) const {
  unsigned pos = curOffset + offset;
  if (pos >= buffer.size())
    return '\0';
  return buffer[pos];
}

char Lexer::advanceChar() {
  if (curOffset >= buffer.size())
    return '\0';
  return buffer[curOffset++];
}

SourceLocation Lexer::currentLoc() const {
  return SourceLocation(fileID, curOffset);
}

bool Lexer::isEOF() const {
  return curOffset >= buffer.size() && !hasPeeked;
}

Token Lexer::lex() {
  if (hasPeeked) {
    hasPeeked = false;
    return peekedToken;
  }
  Token result;
  lexImpl(result);
  return result;
}

const Token &Lexer::peek() {
  if (!hasPeeked) {
    lexImpl(peekedToken);
    hasPeeked = true;
  }
  return peekedToken;
}

static bool isIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool isIdentContinue(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static bool isDigit(char c) {
  return std::isdigit(static_cast<unsigned char>(c));
}

static bool isHexDigit(char c) {
  return std::isxdigit(static_cast<unsigned char>(c));
}

static bool isOctalDigit(char c) { return c >= '0' && c <= '7'; }

static bool isBinaryDigit(char c) { return c == '0' || c == '1'; }

void Lexer::lexImpl(Token &result) {
restart:
  // Skip whitespace.
  while (curOffset < buffer.size()) {
    char c = buffer[curOffset];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
      ++curOffset;
    else
      break;
  }

  if (curOffset >= buffer.size()) {
    result = Token(tok::eof, currentLoc(), "");
    return;
  }

  // If we're inside a template literal and see }, lex the continuation.
  if (templateDepth > 0 && peekChar() == '}') {
    advanceChar(); // consume }
    lexTemplateLiteral(result);
    return;
  }

  SourceLocation startLoc = currentLoc();
  char c = peekChar();

  // Identifiers and keywords.
  if (isIdentStart(c)) {
    lexIdentifierOrKeyword(result);
    return;
  }

  // Numeric literals.
  if (isDigit(c)) {
    lexNumericLiteral(result);
    return;
  }

  // String literal.
  if (c == '"') {
    lexStringLiteral(result);
    return;
  }

  // Char literal.
  if (c == '\'') {
    lexCharLiteral(result);
    return;
  }

  // Template literal.
  if (c == '`') {
    advanceChar(); // consume `
    lexTemplateLiteral(result);
    return;
  }

  // Raw string: r"..." or r#"..."#
  if (c == 'r' && (peekChar(1) == '"' || peekChar(1) == '#')) {
    lexRawStringLiteral(result);
    return;
  }

  // Attribute: @name
  if (c == '@') {
    lexAttribute(result);
    return;
  }

  // Comments.
  if (c == '/' && peekChar(1) == '/') {
    if (peekChar(2) == '/') {
      // Doc line comment: ///
      lexDocLineComment(result);
      return;
    }
    skipLineComment();
    goto restart;
  }
  if (c == '/' && peekChar(1) == '*') {
    if (peekChar(2) == '*' && peekChar(3) != '/') {
      // Doc block comment: /** ... */
      lexDocBlockComment(result);
      return;
    }
    skipBlockComment();
    goto restart;
  }

  // Operators and punctuators.
  advanceChar(); // consume first char

  switch (c) {
  case '(':
    result = Token(tok::l_paren, startLoc, "(");
    return;
  case ')':
    result = Token(tok::r_paren, startLoc, ")");
    return;
  case '[':
    result = Token(tok::l_bracket, startLoc, "[");
    return;
  case ']':
    result = Token(tok::r_bracket, startLoc, "]");
    return;
  case '{':
    result = Token(tok::l_brace, startLoc, "{");
    return;
  case '}':
    result = Token(tok::r_brace, startLoc, "}");
    return;
  case ',':
    result = Token(tok::comma, startLoc, ",");
    return;
  case ';':
    result = Token(tok::semicolon, startLoc, ";");
    return;
  case '~':
    result = Token(tok::tilde, startLoc, "~");
    return;
  case '#':
    result = Token(tok::hash, startLoc, "#");
    return;
  case '?':
    result = Token(tok::question, startLoc, "?");
    return;

  case '.':
    if (peekChar() == '.') {
      advanceChar();
      if (peekChar() == '=') {
        advanceChar();
        result = Token(tok::dotdotequal, startLoc, "..=");
      } else {
        result = Token(tok::dotdot, startLoc, "..");
      }
    } else {
      result = Token(tok::dot, startLoc, ".");
    }
    return;

  case ':':
    if (peekChar() == ':') {
      advanceChar();
      result = Token(tok::coloncolon, startLoc, "::");
    } else {
      result = Token(tok::colon, startLoc, ":");
    }
    return;

  case '+':
    if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::plusequal, startLoc, "+=");
    } else {
      result = Token(tok::plus, startLoc, "+");
    }
    return;

  case '-':
    if (peekChar() == '>') {
      advanceChar();
      result = Token(tok::arrow, startLoc, "->");
    } else if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::minusequal, startLoc, "-=");
    } else {
      result = Token(tok::minus, startLoc, "-");
    }
    return;

  case '*':
    if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::starequal, startLoc, "*=");
    } else {
      result = Token(tok::star, startLoc, "*");
    }
    return;

  case '/':
    if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::slashequal, startLoc, "/=");
    } else {
      result = Token(tok::slash, startLoc, "/");
    }
    return;

  case '%':
    if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::percentequal, startLoc, "%=");
    } else {
      result = Token(tok::percent, startLoc, "%");
    }
    return;

  case '&':
    if (peekChar() == '&') {
      advanceChar();
      result = Token(tok::ampamp, startLoc, "&&");
    } else if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::ampequal, startLoc, "&=");
    } else {
      result = Token(tok::amp, startLoc, "&");
    }
    return;

  case '|':
    if (peekChar() == '|') {
      advanceChar();
      result = Token(tok::pipepipe, startLoc, "||");
    } else if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::pipeequal, startLoc, "|=");
    } else {
      result = Token(tok::pipe, startLoc, "|");
    }
    return;

  case '^':
    if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::caretequal, startLoc, "^=");
    } else {
      result = Token(tok::caret, startLoc, "^");
    }
    return;

  case '!':
    if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::exclaimequal, startLoc, "!=");
    } else {
      result = Token(tok::exclaim, startLoc, "!");
    }
    return;

  case '<':
    if (peekChar() == '<') {
      advanceChar();
      if (peekChar() == '=') {
        advanceChar();
        result = Token(tok::lesslessequal, startLoc, "<<=");
      } else {
        result = Token(tok::lessless, startLoc, "<<");
      }
    } else if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::lessequal, startLoc, "<=");
    } else {
      result = Token(tok::less, startLoc, "<");
    }
    return;

  case '>':
    if (peekChar() == '>') {
      advanceChar();
      if (peekChar() == '=') {
        advanceChar();
        result = Token(tok::greatergreaterequal, startLoc, ">>=");
      } else {
        result = Token(tok::greatergreater, startLoc, ">>");
      }
    } else if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::greaterequal, startLoc, ">=");
    } else {
      result = Token(tok::greater, startLoc, ">");
    }
    return;

  case '=':
    if (peekChar() == '=') {
      advanceChar();
      result = Token(tok::equalequal, startLoc, "==");
    } else if (peekChar() == '>') {
      advanceChar();
      result = Token(tok::fat_arrow, startLoc, "=>");
    } else {
      result = Token(tok::equal, startLoc, "=");
    }
    return;

  default:
    diags.emitError(startLoc, DiagID::ErrUnexpectedToken,
                    std::string("unexpected character '") + c + "'");
    result = Token(tok::unknown, startLoc,
                   llvm::StringRef(&buffer[curOffset - 1], 1));
    return;
  }
}

void Lexer::lexIdentifierOrKeyword(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;
  while (curOffset < buffer.size() && isIdentContinue(buffer[curOffset]))
    ++curOffset;
  llvm::StringRef spelling = buffer.slice(start, curOffset);

  // Check if it's a keyword.
  tok::TokenKind kind = tok::getKeywordTokenKind(spelling);
  result = Token(kind, startLoc, spelling);
}

void Lexer::lexNumericLiteral(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;

  char first = advanceChar();
  bool isFloat = false;

  if (first == '0' && curOffset < buffer.size()) {
    char next = peekChar();
    if (next == 'x' || next == 'X') {
      // Hex literal.
      advanceChar();
      while (curOffset < buffer.size() &&
             (isHexDigit(buffer[curOffset]) || buffer[curOffset] == '_'))
        ++curOffset;
      goto suffix;
    } else if (next == 'o' || next == 'O') {
      // Octal literal.
      advanceChar();
      while (curOffset < buffer.size() &&
             (isOctalDigit(buffer[curOffset]) || buffer[curOffset] == '_'))
        ++curOffset;
      goto suffix;
    } else if (next == 'b' || next == 'B') {
      // Binary literal.
      advanceChar();
      while (curOffset < buffer.size() &&
             (isBinaryDigit(buffer[curOffset]) || buffer[curOffset] == '_'))
        ++curOffset;
      goto suffix;
    }
  }

  // Consume decimal digits and underscores.
  while (curOffset < buffer.size() &&
         (isDigit(buffer[curOffset]) || buffer[curOffset] == '_'))
    ++curOffset;

  // Check for float: decimal point followed by digit (not ..).
  if (curOffset < buffer.size() && buffer[curOffset] == '.' &&
      curOffset + 1 < buffer.size() && buffer[curOffset + 1] != '.' &&
      isDigit(buffer[curOffset + 1])) {
    isFloat = true;
    advanceChar(); // consume '.'
    while (curOffset < buffer.size() &&
           (isDigit(buffer[curOffset]) || buffer[curOffset] == '_'))
      ++curOffset;
  }

  // Exponent part.
  if (curOffset < buffer.size() &&
      (buffer[curOffset] == 'e' || buffer[curOffset] == 'E')) {
    isFloat = true;
    advanceChar();
    if (curOffset < buffer.size() &&
        (buffer[curOffset] == '+' || buffer[curOffset] == '-'))
      advanceChar();
    while (curOffset < buffer.size() &&
           (isDigit(buffer[curOffset]) || buffer[curOffset] == '_'))
      ++curOffset;
  }

suffix: {
  // Type suffix: i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, f32, f64,
  // usize, isize.
  std::string suffixStr;

  if (curOffset < buffer.size() && isIdentStart(buffer[curOffset])) {
    unsigned suffStart = curOffset;
    while (curOffset < buffer.size() && isIdentContinue(buffer[curOffset]))
      ++curOffset;
    llvm::StringRef potentialSuffix = buffer.slice(suffStart, curOffset);
    bool validSuffix =
        llvm::StringSwitch<bool>(potentialSuffix)
            .Cases("i8", "i16", "i32", "i64", "i128", true)
            .Cases("u8", "u16", "u32", "u64", "u128", true)
            .Cases("f32", "f64", true)
            .Cases("usize", "isize", true)
            .Default(false);
    if (validSuffix) {
      suffixStr = potentialSuffix.str();
      if (potentialSuffix.starts_with("f"))
        isFloat = true;
    } else {
      // Not a valid suffix — roll back.
      curOffset = suffStart;
    }
  }

  llvm::StringRef spelling = buffer.slice(start, curOffset);

  if (isFloat) {
    result = Token(tok::float_literal, startLoc, spelling);
    std::string clean;
    for (char ch : spelling)
      if (ch != '_')
        clean += ch;
    if (!suffixStr.empty())
      clean = clean.substr(0, clean.size() - suffixStr.size());
    try {
      result.setFloatValue(std::stod(clean));
    } catch (...) {
      diags.emitError(startLoc, DiagID::ErrInvalidLiteral,
                      "invalid float literal");
      result.setFloatValue(0.0);
    }
  } else {
    result = Token(tok::integer_literal, startLoc, spelling);
    std::string clean;
    for (char ch : spelling)
      if (ch != '_')
        clean += ch;
    if (!suffixStr.empty())
      clean = clean.substr(0, clean.size() - suffixStr.size());
    uint64_t val = 0;
    int base = 10;
    llvm::StringRef digits(clean);
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
      base = 16;
      digits = digits.drop_front(2);
    } else if (digits.starts_with("0o") || digits.starts_with("0O")) {
      base = 8;
      digits = digits.drop_front(2);
    } else if (digits.starts_with("0b") || digits.starts_with("0B")) {
      base = 2;
      digits = digits.drop_front(2);
    }
    if (!digits.empty()) {
      auto [ptr, ec] = std::from_chars(digits.data(),
                                        digits.data() + digits.size(), val, base);
      if (ec != std::errc()) {
        diags.emitError(startLoc, DiagID::ErrInvalidLiteral,
                        "invalid integer literal");
      }
    }
    result.setIntegerValue(val);
  }

  if (!suffixStr.empty())
    result.setSuffixType(suffixStr);
  }
}

bool Lexer::lexEscapeSequence(std::string &out) {
  if (curOffset >= buffer.size()) {
    diags.emitError(currentLoc(), DiagID::ErrInvalidEscape,
                    "unexpected end of file in escape sequence");
    return false;
  }
  char c = advanceChar();
  switch (c) {
  case 'n':  out += '\n'; return true;
  case 't':  out += '\t'; return true;
  case 'r':  out += '\r'; return true;
  case '\\': out += '\\'; return true;
  case '\'': out += '\''; return true;
  case '"':  out += '"';  return true;
  case '0':  out += '\0'; return true;
  case '`':  out += '`';  return true;
  case '$':  out += '$';  return true;
  case 'u': {
    if (peekChar() != '{') {
      diags.emitError(currentLoc(), DiagID::ErrInvalidEscape,
                      "expected '{' in Unicode escape");
      return false;
    }
    advanceChar(); // consume {
    unsigned codeStart = curOffset;
    while (curOffset < buffer.size() && buffer[curOffset] != '}')
      ++curOffset;
    if (curOffset >= buffer.size()) {
      diags.emitError(currentLoc(), DiagID::ErrInvalidEscape,
                      "unterminated Unicode escape");
      return false;
    }
    llvm::StringRef hex = buffer.slice(codeStart, curOffset);
    advanceChar(); // consume }
    uint32_t codePoint = 0;
    auto [ptr, ec] =
        std::from_chars(hex.data(), hex.data() + hex.size(), codePoint, 16);
    if (ec != std::errc() || codePoint > 0x10FFFF) {
      diags.emitError(currentLoc(), DiagID::ErrInvalidEscape,
                      "invalid Unicode code point");
      return false;
    }
    // Encode as UTF-8.
    if (codePoint < 0x80) {
      out += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
      out += static_cast<char>(0xC0 | (codePoint >> 6));
      out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
      out += static_cast<char>(0xE0 | (codePoint >> 12));
      out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (codePoint >> 18));
      out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
    return true;
  }
  default:
    diags.emitError(currentLoc(), DiagID::ErrInvalidEscape,
                    std::string("invalid escape sequence '\\") + c + "'");
    out += c;
    return false;
  }
}

void Lexer::lexStringLiteral(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;
  advanceChar(); // consume opening "
  std::string value;

  while (curOffset < buffer.size()) {
    char c = buffer[curOffset];
    if (c == '"') {
      advanceChar(); // consume closing "
      result = Token(tok::string_literal, startLoc,
                     buffer.slice(start, curOffset));
      return;
    }
    if (c == '\\') {
      advanceChar();
      lexEscapeSequence(value);
    } else if (c == '\n') {
      diags.emitError(currentLoc(), DiagID::ErrUnterminatedString,
                      "unterminated string literal");
      result = Token(tok::string_literal, startLoc,
                     buffer.slice(start, curOffset));
      return;
    } else {
      value += c;
      advanceChar();
    }
  }

  diags.emitError(startLoc, DiagID::ErrUnterminatedString,
                  "unterminated string literal");
  result = Token(tok::string_literal, startLoc, buffer.slice(start, curOffset));
}

void Lexer::lexCharLiteral(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;
  advanceChar(); // consume opening '
  std::string value;

  if (curOffset < buffer.size() && buffer[curOffset] == '\\') {
    advanceChar();
    lexEscapeSequence(value);
  } else if (curOffset < buffer.size()) {
    value += advanceChar();
  }

  if (curOffset < buffer.size() && buffer[curOffset] == '\'') {
    advanceChar(); // consume closing '
  } else {
    diags.emitError(startLoc, DiagID::ErrUnterminatedString,
                    "unterminated character literal");
  }

  result = Token(tok::char_literal, startLoc, buffer.slice(start, curOffset));
}

void Lexer::lexTemplateLiteral(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;
  std::string value;
  // DECISION: Determine if this is head or middle based on templateDepth.
  // If templateDepth was 0 before entering, it's a head/no-sub.
  // If > 0, it's middle/tail (we came from a } inside ${...}).
  bool isHead = (templateDepth == 0);

  while (curOffset < buffer.size()) {
    char c = buffer[curOffset];
    if (c == '`') {
      advanceChar();
      llvm::StringRef spelling = buffer.slice(start, curOffset);
      if (isHead) {
        result = Token(tok::template_no_sub, startLoc, spelling);
      } else {
        result = Token(tok::template_tail, startLoc, spelling);
        --templateDepth;
      }
      return;
    }
    if (c == '$' && peekChar(1) == '{') {
      llvm::StringRef spelling = buffer.slice(start, curOffset);
      advanceChar(); // consume $
      advanceChar(); // consume {
      ++templateDepth;
      if (isHead) {
        result = Token(tok::template_head, startLoc, spelling);
      } else {
        result = Token(tok::template_middle, startLoc, spelling);
      }
      return;
    }
    if (c == '\\') {
      advanceChar();
      lexEscapeSequence(value);
    } else {
      value += c;
      advanceChar();
    }
  }

  diags.emitError(startLoc, DiagID::ErrUnterminatedString,
                  "unterminated template literal");
  result = Token(tok::template_no_sub, startLoc,
                 buffer.slice(start, curOffset));
}

void Lexer::lexRawStringLiteral(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;
  advanceChar(); // consume 'r'

  unsigned hashCount = 0;
  while (peekChar() == '#') {
    advanceChar();
    ++hashCount;
  }

  if (peekChar() != '"') {
    diags.emitError(currentLoc(), DiagID::ErrInvalidLiteral,
                    "expected '\"' after raw string prefix");
    result = Token(tok::unknown, startLoc, buffer.slice(start, curOffset));
    return;
  }
  advanceChar(); // consume opening "

  while (curOffset < buffer.size()) {
    if (buffer[curOffset] == '"') {
      advanceChar();
      unsigned matched = 0;
      while (matched < hashCount && curOffset < buffer.size() &&
             buffer[curOffset] == '#') {
        advanceChar();
        ++matched;
      }
      if (matched == hashCount) {
        result = Token(tok::string_literal, startLoc,
                       buffer.slice(start, curOffset));
        return;
      }
    } else {
      advanceChar();
    }
  }

  diags.emitError(startLoc, DiagID::ErrUnterminatedString,
                  "unterminated raw string literal");
  result = Token(tok::string_literal, startLoc, buffer.slice(start, curOffset));
}

void Lexer::skipLineComment() {
  while (curOffset < buffer.size() && buffer[curOffset] != '\n')
    ++curOffset;
}

void Lexer::skipBlockComment() {
  advanceChar(); // consume /
  advanceChar(); // consume *
  unsigned depth = 1;
  while (curOffset < buffer.size() && depth > 0) {
    if (buffer[curOffset] == '/' && peekChar(1) == '*') {
      advanceChar();
      advanceChar();
      ++depth;
    } else if (buffer[curOffset] == '*' && peekChar(1) == '/') {
      advanceChar();
      advanceChar();
      --depth;
    } else {
      advanceChar();
    }
  }
  if (depth > 0) {
    diags.emitError(currentLoc(), DiagID::ErrUnterminatedComment,
                    "unterminated block comment");
  }
}

void Lexer::lexDocLineComment(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;
  while (curOffset < buffer.size() && buffer[curOffset] != '\n')
    ++curOffset;
  result = Token(tok::doc_line_comment, startLoc,
                 buffer.slice(start, curOffset));
}

void Lexer::lexDocBlockComment(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;
  advanceChar(); // /
  advanceChar(); // *
  advanceChar(); // *
  unsigned depth = 1;
  while (curOffset < buffer.size() && depth > 0) {
    if (buffer[curOffset] == '/' && peekChar(1) == '*') {
      advanceChar();
      advanceChar();
      ++depth;
    } else if (buffer[curOffset] == '*' && peekChar(1) == '/') {
      advanceChar();
      advanceChar();
      --depth;
    } else {
      advanceChar();
    }
  }
  if (depth > 0) {
    diags.emitError(startLoc, DiagID::ErrUnterminatedComment,
                    "unterminated doc block comment");
  }
  result = Token(tok::doc_block_comment, startLoc,
                 buffer.slice(start, curOffset));
}

void Lexer::lexAttribute(Token &result) {
  SourceLocation startLoc = currentLoc();
  unsigned start = curOffset;
  advanceChar(); // consume @

  if (curOffset < buffer.size() && isIdentStart(buffer[curOffset])) {
    while (curOffset < buffer.size() && isIdentContinue(buffer[curOffset]))
      ++curOffset;
    // If followed by (, consume the parenthesized argument.
    if (curOffset < buffer.size() && buffer[curOffset] == '(') {
      advanceChar();
      unsigned depth = 1;
      while (curOffset < buffer.size() && depth > 0) {
        if (buffer[curOffset] == '(')
          ++depth;
        else if (buffer[curOffset] == ')')
          --depth;
        if (depth > 0)
          advanceChar();
      }
      if (curOffset < buffer.size())
        advanceChar(); // consume closing )
    }
  }

  result = Token(tok::attribute, startLoc, buffer.slice(start, curOffset));
}

} // namespace asc
