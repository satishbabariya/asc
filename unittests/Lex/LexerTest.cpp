#include "asc/Basic/Diagnostic.h"
#include "asc/Basic/DiagnosticIDs.h"
#include "asc/Basic/SourceLocation.h"
#include "asc/Basic/SourceManager.h"
#include "asc/Basic/TokenKinds.h"
#include "asc/Lex/Lexer.h"
#include "asc/Lex/Token.h"
#include "gtest/gtest.h"
#include <vector>

using namespace asc;

// Helper: lex all tokens from source string.
static std::vector<Token> lexAll(llvm::StringRef source) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", source);
  DiagnosticEngine diags(sm);
  std::string diagOutput;
  llvm::raw_string_ostream os(diagOutput);
  diags.setOutputStream(os);

  Lexer lexer(fid, sm, diags);
  std::vector<Token> tokens;
  while (true) {
    Token tok = lexer.lex();
    if (tok.is(tok::eof))
      break;
    tokens.push_back(tok);
  }
  return tokens;
}

// --- SourceManager Tests ---

TEST(SourceManagerTest, CreateBuffer) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "hello\nworld\n");
  ASSERT_TRUE(fid.isValid());
  EXPECT_EQ(sm.getFilename(fid), "test.ts");
  EXPECT_EQ(sm.getBufferData(fid), "hello\nworld\n");
}

TEST(SourceManagerTest, LineColumn) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "abc\ndef\nghi\n");

  auto lc = sm.getLineAndColumn(SourceLocation(fid, 0));
  EXPECT_EQ(lc.line, 1u);
  EXPECT_EQ(lc.column, 1u);

  lc = sm.getLineAndColumn(SourceLocation(fid, 4));
  EXPECT_EQ(lc.line, 2u);
  EXPECT_EQ(lc.column, 1u);

  lc = sm.getLineAndColumn(SourceLocation(fid, 6));
  EXPECT_EQ(lc.line, 2u);
  EXPECT_EQ(lc.column, 3u);

  lc = sm.getLineAndColumn(SourceLocation(fid, 8));
  EXPECT_EQ(lc.line, 3u);
  EXPECT_EQ(lc.column, 1u);
}

TEST(SourceManagerTest, GetSourceLine) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "first line\nsecond line\nthird\n");

  EXPECT_EQ(sm.getSourceLine(SourceLocation(fid, 0)), "first line");
  EXPECT_EQ(sm.getSourceLine(SourceLocation(fid, 11)), "second line");
  EXPECT_EQ(sm.getSourceLine(SourceLocation(fid, 23)), "third");
}

TEST(SourceManagerTest, MultipleFiles) {
  SourceManager sm;
  FileID fid1 = sm.createBuffer("a.ts", "aaa");
  FileID fid2 = sm.createBuffer("b.ts", "bbb");
  EXPECT_NE(fid1.getID(), fid2.getID());
  EXPECT_EQ(sm.getBufferData(fid1), "aaa");
  EXPECT_EQ(sm.getBufferData(fid2), "bbb");
}

// --- DiagnosticEngine Tests ---

TEST(DiagnosticTest, EmitError) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "let x = 1;\nlet y = x;\n");
  DiagnosticEngine diags(sm);

  std::string output;
  llvm::raw_string_ostream os(output);
  diags.setOutputStream(os);

  diags.emitError(SourceLocation(fid, 4), DiagID::ErrUseAfterMove,
                  "use of moved value 'x'");

  EXPECT_TRUE(diags.hasErrors());
  EXPECT_EQ(diags.getErrorCount(), 1u);
  EXPECT_TRUE(output.find("E004") != std::string::npos);
  EXPECT_TRUE(output.find("use of moved value") != std::string::npos);
}

TEST(DiagnosticTest, EmitWarning) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "let big: own<BigStruct> = copy(x);\n");
  DiagnosticEngine diags(sm);

  std::string output;
  llvm::raw_string_ostream os(output);
  diags.setOutputStream(os);

  diags.emitWarning(SourceLocation(fid, 0), DiagID::WarnLargeCopyType,
                    "large @copy type (256 bytes); consider own<T>");

  EXPECT_FALSE(diags.hasErrors());
  EXPECT_EQ(diags.getWarningCount(), 1u);
  EXPECT_TRUE(output.find("W002") != std::string::npos);
}

TEST(DiagnosticTest, DiagBuilder) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "let x = 1;\nlet y = &x;\ndrop(x);\n");
  DiagnosticEngine diags(sm);

  std::string output;
  llvm::raw_string_ostream os(output);
  diags.setOutputStream(os);

  {
    auto builder = diags.report(SourceLocation(fid, 25),
                                DiagID::ErrBorrowOutlivesOwner,
                                "cannot drop 'x' while borrowed");
    builder.addNote(SourceLocation(fid, 15), "borrow of 'x' occurs here");
  }

  EXPECT_TRUE(diags.hasErrors());
  EXPECT_EQ(diags.getDiagnostics().size(), 1u);
  EXPECT_EQ(diags.getDiagnostics()[0].notes.size(), 1u);
}

TEST(DiagnosticTest, JSONFormat) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "let x = 1;\n");
  DiagnosticEngine diags(sm);
  diags.setErrorFormat(ErrorFormat::JSON);

  std::string output;
  llvm::raw_string_ostream os(output);
  diags.setOutputStream(os);

  diags.emitError(SourceLocation(fid, 4), DiagID::ErrTypeMismatch,
                  "type mismatch");

  EXPECT_TRUE(output.find("\"severity\":\"error\"") != std::string::npos);
  EXPECT_TRUE(output.find("\"message\":\"type mismatch\"") != std::string::npos);
}

TEST(DiagnosticTest, GithubActionsFormat) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "let x = 1;\n");
  DiagnosticEngine diags(sm);
  diags.setErrorFormat(ErrorFormat::GithubActions);

  std::string output;
  llvm::raw_string_ostream os(output);
  diags.setOutputStream(os);

  diags.emitError(SourceLocation(fid, 4), DiagID::ErrTypeMismatch,
                  "type mismatch");

  EXPECT_TRUE(output.find("::error") != std::string::npos);
  EXPECT_TRUE(output.find("file=test.ts") != std::string::npos);
}

// --- TokenKinds Tests ---

TEST(TokenKindsTest, KeywordLookup) {
  EXPECT_EQ(tok::getKeywordTokenKind("const"), tok::kw_const);
  EXPECT_EQ(tok::getKeywordTokenKind("let"), tok::kw_let);
  EXPECT_EQ(tok::getKeywordTokenKind("function"), tok::kw_function);
  EXPECT_EQ(tok::getKeywordTokenKind("own"), tok::kw_own);
  EXPECT_EQ(tok::getKeywordTokenKind("ref"), tok::kw_ref);
  EXPECT_EQ(tok::getKeywordTokenKind("refmut"), tok::kw_refmut);
  EXPECT_EQ(tok::getKeywordTokenKind("notakeyword"), tok::identifier);
}

TEST(TokenKindsTest, IsKeyword) {
  EXPECT_TRUE(tok::isKeyword(tok::kw_const));
  EXPECT_TRUE(tok::isKeyword(tok::kw_struct));
  EXPECT_FALSE(tok::isKeyword(tok::identifier));
  EXPECT_FALSE(tok::isKeyword(tok::plus));
}

TEST(TokenKindsTest, PunctuatorSpelling) {
  EXPECT_STREQ(tok::getPunctuatorSpelling(tok::plus), "+");
  EXPECT_STREQ(tok::getPunctuatorSpelling(tok::fat_arrow), "=>");
  EXPECT_STREQ(tok::getPunctuatorSpelling(tok::coloncolon), "::");
  EXPECT_STREQ(tok::getPunctuatorSpelling(tok::dotdotequal), "..=");
}

// --- Lexer Tests ---

TEST(LexerTest, EmptyInput) {
  auto tokens = lexAll("");
  EXPECT_TRUE(tokens.empty());
}

TEST(LexerTest, Keywords) {
  auto tokens = lexAll("const let function fn return if else while loop for");
  ASSERT_EQ(tokens.size(), 10u);
  EXPECT_EQ(tokens[0].getKind(), tok::kw_const);
  EXPECT_EQ(tokens[1].getKind(), tok::kw_let);
  EXPECT_EQ(tokens[2].getKind(), tok::kw_function);
  EXPECT_EQ(tokens[3].getKind(), tok::kw_fn);
  EXPECT_EQ(tokens[4].getKind(), tok::kw_return);
  EXPECT_EQ(tokens[5].getKind(), tok::kw_if);
  EXPECT_EQ(tokens[6].getKind(), tok::kw_else);
  EXPECT_EQ(tokens[7].getKind(), tok::kw_while);
  EXPECT_EQ(tokens[8].getKind(), tok::kw_loop);
  EXPECT_EQ(tokens[9].getKind(), tok::kw_for);
}

TEST(LexerTest, OwnershipKeywords) {
  auto tokens = lexAll("own ref refmut task chan");
  ASSERT_EQ(tokens.size(), 5u);
  EXPECT_EQ(tokens[0].getKind(), tok::kw_own);
  EXPECT_EQ(tokens[1].getKind(), tok::kw_ref);
  EXPECT_EQ(tokens[2].getKind(), tok::kw_refmut);
  EXPECT_EQ(tokens[3].getKind(), tok::kw_task);
  EXPECT_EQ(tokens[4].getKind(), tok::kw_chan);
}

TEST(LexerTest, MoreKeywords) {
  auto tokens = lexAll("struct enum trait impl match break continue of");
  ASSERT_EQ(tokens.size(), 8u);
  EXPECT_EQ(tokens[0].getKind(), tok::kw_struct);
  EXPECT_EQ(tokens[1].getKind(), tok::kw_enum);
  EXPECT_EQ(tokens[2].getKind(), tok::kw_trait);
  EXPECT_EQ(tokens[3].getKind(), tok::kw_impl);
  EXPECT_EQ(tokens[4].getKind(), tok::kw_match);
  EXPECT_EQ(tokens[5].getKind(), tok::kw_break);
  EXPECT_EQ(tokens[6].getKind(), tok::kw_continue);
  EXPECT_EQ(tokens[7].getKind(), tok::kw_of);
}

TEST(LexerTest, Identifiers) {
  auto tokens = lexAll("foo bar_baz _private x123");
  ASSERT_EQ(tokens.size(), 4u);
  for (auto &t : tokens)
    EXPECT_EQ(t.getKind(), tok::identifier);
  EXPECT_EQ(tokens[0].getSpelling(), "foo");
  EXPECT_EQ(tokens[1].getSpelling(), "bar_baz");
  EXPECT_EQ(tokens[2].getSpelling(), "_private");
  EXPECT_EQ(tokens[3].getSpelling(), "x123");
}

TEST(LexerTest, IntegerLiterals) {
  auto tokens = lexAll("42 0 1_000_000");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::integer_literal);
  EXPECT_EQ(tokens[0].getIntegerValue(), 42u);
  EXPECT_EQ(tokens[1].getIntegerValue(), 0u);
  EXPECT_EQ(tokens[2].getIntegerValue(), 1000000u);
}

TEST(LexerTest, HexOctalBinaryLiterals) {
  auto tokens = lexAll("0xFF 0o77 0b1010");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getIntegerValue(), 0xFFu);
  EXPECT_EQ(tokens[1].getIntegerValue(), 077u);
  EXPECT_EQ(tokens[2].getIntegerValue(), 0b1010u);
}

TEST(LexerTest, IntegerSuffixes) {
  auto tokens = lexAll("42i32 100u64 7i8");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::integer_literal);
  EXPECT_EQ(tokens[0].getIntegerValue(), 42u);
  EXPECT_EQ(tokens[0].getSuffixType(), "i32");
  EXPECT_EQ(tokens[1].getIntegerValue(), 100u);
  EXPECT_EQ(tokens[1].getSuffixType(), "u64");
  EXPECT_EQ(tokens[2].getIntegerValue(), 7u);
  EXPECT_EQ(tokens[2].getSuffixType(), "i8");
}

TEST(LexerTest, FloatLiterals) {
  auto tokens = lexAll("3.14 0.5 1.0e10");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::float_literal);
  EXPECT_DOUBLE_EQ(tokens[0].getFloatValue(), 3.14);
  EXPECT_DOUBLE_EQ(tokens[1].getFloatValue(), 0.5);
  EXPECT_DOUBLE_EQ(tokens[2].getFloatValue(), 1.0e10);
}

TEST(LexerTest, FloatSuffix) {
  auto tokens = lexAll("3.14f32");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].getKind(), tok::float_literal);
  EXPECT_EQ(tokens[0].getSuffixType(), "f32");
}

TEST(LexerTest, StringLiteral) {
  auto tokens = lexAll("\"hello world\"");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].getKind(), tok::string_literal);
  EXPECT_EQ(tokens[0].getSpelling(), "\"hello world\"");
}

TEST(LexerTest, StringEscapes) {
  auto tokens = lexAll("\"hello\\nworld\"");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].getKind(), tok::string_literal);
}

TEST(LexerTest, CharLiteral) {
  auto tokens = lexAll("'a' '\\n' '\\0'");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::char_literal);
  EXPECT_EQ(tokens[1].getKind(), tok::char_literal);
  EXPECT_EQ(tokens[2].getKind(), tok::char_literal);
}

TEST(LexerTest, SimpleOperators) {
  auto tokens = lexAll("+ - * / % = ! < > & | ^ ~");
  ASSERT_EQ(tokens.size(), 13u);
  EXPECT_EQ(tokens[0].getKind(), tok::plus);
  EXPECT_EQ(tokens[1].getKind(), tok::minus);
  EXPECT_EQ(tokens[2].getKind(), tok::star);
  EXPECT_EQ(tokens[3].getKind(), tok::slash);
  EXPECT_EQ(tokens[4].getKind(), tok::percent);
  EXPECT_EQ(tokens[5].getKind(), tok::equal);
  EXPECT_EQ(tokens[6].getKind(), tok::exclaim);
  EXPECT_EQ(tokens[7].getKind(), tok::less);
  EXPECT_EQ(tokens[8].getKind(), tok::greater);
  EXPECT_EQ(tokens[9].getKind(), tok::amp);
  EXPECT_EQ(tokens[10].getKind(), tok::pipe);
  EXPECT_EQ(tokens[11].getKind(), tok::caret);
  EXPECT_EQ(tokens[12].getKind(), tok::tilde);
}

TEST(LexerTest, CompoundOperators) {
  auto tokens = lexAll("+= -= *= /= %= &= |= ^= <<= >>=");
  ASSERT_EQ(tokens.size(), 10u);
  EXPECT_EQ(tokens[0].getKind(), tok::plusequal);
  EXPECT_EQ(tokens[1].getKind(), tok::minusequal);
  EXPECT_EQ(tokens[2].getKind(), tok::starequal);
  EXPECT_EQ(tokens[3].getKind(), tok::slashequal);
  EXPECT_EQ(tokens[4].getKind(), tok::percentequal);
  EXPECT_EQ(tokens[5].getKind(), tok::ampequal);
  EXPECT_EQ(tokens[6].getKind(), tok::pipeequal);
  EXPECT_EQ(tokens[7].getKind(), tok::caretequal);
  EXPECT_EQ(tokens[8].getKind(), tok::lesslessequal);
  EXPECT_EQ(tokens[9].getKind(), tok::greatergreaterequal);
}

TEST(LexerTest, ComparisonOperators) {
  auto tokens = lexAll("== != <= >= << >>");
  ASSERT_EQ(tokens.size(), 6u);
  EXPECT_EQ(tokens[0].getKind(), tok::equalequal);
  EXPECT_EQ(tokens[1].getKind(), tok::exclaimequal);
  EXPECT_EQ(tokens[2].getKind(), tok::lessequal);
  EXPECT_EQ(tokens[3].getKind(), tok::greaterequal);
  EXPECT_EQ(tokens[4].getKind(), tok::lessless);
  EXPECT_EQ(tokens[5].getKind(), tok::greatergreater);
}

TEST(LexerTest, LogicalOperators) {
  auto tokens = lexAll("&& ||");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getKind(), tok::ampamp);
  EXPECT_EQ(tokens[1].getKind(), tok::pipepipe);
}

TEST(LexerTest, Arrows) {
  auto tokens = lexAll("-> =>");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getKind(), tok::arrow);
  EXPECT_EQ(tokens[1].getKind(), tok::fat_arrow);
}

TEST(LexerTest, Delimiters) {
  auto tokens = lexAll("( ) [ ] { }");
  ASSERT_EQ(tokens.size(), 6u);
  EXPECT_EQ(tokens[0].getKind(), tok::l_paren);
  EXPECT_EQ(tokens[1].getKind(), tok::r_paren);
  EXPECT_EQ(tokens[2].getKind(), tok::l_bracket);
  EXPECT_EQ(tokens[3].getKind(), tok::r_bracket);
  EXPECT_EQ(tokens[4].getKind(), tok::l_brace);
  EXPECT_EQ(tokens[5].getKind(), tok::r_brace);
}

TEST(LexerTest, Dots) {
  auto tokens = lexAll(". .. ..=");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::dot);
  EXPECT_EQ(tokens[1].getKind(), tok::dotdot);
  EXPECT_EQ(tokens[2].getKind(), tok::dotdotequal);
}

TEST(LexerTest, Colons) {
  auto tokens = lexAll(": ::");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getKind(), tok::colon);
  EXPECT_EQ(tokens[1].getKind(), tok::coloncolon);
}

TEST(LexerTest, LineCommentSkipped) {
  auto tokens = lexAll("foo // comment\nbar");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getSpelling(), "foo");
  EXPECT_EQ(tokens[1].getSpelling(), "bar");
}

TEST(LexerTest, BlockCommentSkipped) {
  auto tokens = lexAll("foo /* block */ bar");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getSpelling(), "foo");
  EXPECT_EQ(tokens[1].getSpelling(), "bar");
}

TEST(LexerTest, NestedBlockComment) {
  auto tokens = lexAll("a /* outer /* inner */ still comment */ b");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getSpelling(), "a");
  EXPECT_EQ(tokens[1].getSpelling(), "b");
}

TEST(LexerTest, DocLineComment) {
  auto tokens = lexAll("/// doc comment\nfoo");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getKind(), tok::doc_line_comment);
  EXPECT_EQ(tokens[1].getSpelling(), "foo");
}

TEST(LexerTest, DocBlockComment) {
  auto tokens = lexAll("/** doc block */ foo");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getKind(), tok::doc_block_comment);
  EXPECT_EQ(tokens[1].getSpelling(), "foo");
}

TEST(LexerTest, Attribute) {
  auto tokens = lexAll("@copy struct Foo");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::attribute);
  EXPECT_EQ(tokens[0].getSpelling(), "@copy");
  EXPECT_EQ(tokens[1].getKind(), tok::kw_struct);
}

TEST(LexerTest, AttributeWithArgs) {
  auto tokens = lexAll("@repr(C) struct");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getKind(), tok::attribute);
  EXPECT_EQ(tokens[0].getSpelling(), "@repr(C)");
}

TEST(LexerTest, BoolAndNull) {
  auto tokens = lexAll("true false null");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::kw_true);
  EXPECT_EQ(tokens[1].getKind(), tok::kw_false);
  EXPECT_EQ(tokens[2].getKind(), tok::kw_null);
}

TEST(LexerTest, Peek) {
  SourceManager sm;
  FileID fid = sm.createBuffer("test.ts", "a b c");
  DiagnosticEngine diags(sm);
  std::string output;
  llvm::raw_string_ostream os(output);
  diags.setOutputStream(os);

  Lexer lexer(fid, sm, diags);

  // Peek should not advance.
  const Token &peeked = lexer.peek();
  EXPECT_EQ(peeked.getSpelling(), "a");

  // Lex should return the peeked token.
  Token t = lexer.lex();
  EXPECT_EQ(t.getSpelling(), "a");

  // Next token.
  t = lexer.lex();
  EXPECT_EQ(t.getSpelling(), "b");
}

TEST(LexerTest, FunctionDeclaration) {
  auto tokens = lexAll("function add(a: i32, b: i32): i32 { return a + b; }");
  // function add ( a : i32 , b : i32 ) : i32 { return a + b ; }
  ASSERT_GE(tokens.size(), 17u);
  EXPECT_EQ(tokens[0].getKind(), tok::kw_function);
  EXPECT_EQ(tokens[1].getSpelling(), "add");
  EXPECT_EQ(tokens[2].getKind(), tok::l_paren);
}

TEST(LexerTest, StructWithAttributes) {
  auto tokens = lexAll("@copy @send struct Point { x: f64, y: f64 }");
  EXPECT_EQ(tokens[0].getKind(), tok::attribute);
  EXPECT_EQ(tokens[1].getKind(), tok::attribute);
  EXPECT_EQ(tokens[2].getKind(), tok::kw_struct);
}

TEST(LexerTest, RawString) {
  auto tokens = lexAll("r\"hello world\"");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].getKind(), tok::string_literal);
}

TEST(LexerTest, RawStringWithHashes) {
  auto tokens = lexAll("r#\"has \"quotes\" inside\"#");
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].getKind(), tok::string_literal);
}

TEST(LexerTest, QuestionMark) {
  auto tokens = lexAll("result?");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].getKind(), tok::identifier);
  EXPECT_EQ(tokens[1].getKind(), tok::question);
}

TEST(LexerTest, RangeInForLoop) {
  auto tokens = lexAll("0..10");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::integer_literal);
  EXPECT_EQ(tokens[1].getKind(), tok::dotdot);
  EXPECT_EQ(tokens[2].getKind(), tok::integer_literal);
}

TEST(LexerTest, InclusiveRange) {
  auto tokens = lexAll("0..=10");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].getKind(), tok::integer_literal);
  EXPECT_EQ(tokens[1].getKind(), tok::dotdotequal);
  EXPECT_EQ(tokens[2].getKind(), tok::integer_literal);
}

TEST(LexerTest, GenericSyntax) {
  auto tokens = lexAll("own<Vec<i32>>");
  ASSERT_EQ(tokens.size(), 5u);
  EXPECT_EQ(tokens[0].getKind(), tok::kw_own);
  EXPECT_EQ(tokens[1].getKind(), tok::less);
  EXPECT_EQ(tokens[2].getSpelling(), "Vec");
  EXPECT_EQ(tokens[3].getKind(), tok::less);
  // Note: >> is lexed as greatergreater, parser handles splitting
  // Actually with i32 in between: own < Vec < i32 > >
}

TEST(LexerTest, PathExpression) {
  auto tokens = lexAll("Foo::Bar::baz");
  ASSERT_EQ(tokens.size(), 5u);
  EXPECT_EQ(tokens[0].getSpelling(), "Foo");
  EXPECT_EQ(tokens[1].getKind(), tok::coloncolon);
  EXPECT_EQ(tokens[2].getSpelling(), "Bar");
  EXPECT_EQ(tokens[3].getKind(), tok::coloncolon);
  EXPECT_EQ(tokens[4].getSpelling(), "baz");
}

TEST(LexerTest, UnsafeAndDyn) {
  auto tokens = lexAll("unsafe dyn where as");
  ASSERT_EQ(tokens.size(), 4u);
  EXPECT_EQ(tokens[0].getKind(), tok::kw_unsafe);
  EXPECT_EQ(tokens[1].getKind(), tok::kw_dyn);
  EXPECT_EQ(tokens[2].getKind(), tok::kw_where);
  EXPECT_EQ(tokens[3].getKind(), tok::kw_as);
}
