#include "asc/AST/ASTContext.h"
#include "asc/Basic/Diagnostic.h"
#include "asc/Basic/SourceManager.h"
#include "asc/Lex/Lexer.h"
#include "asc/Parse/Parser.h"
#include "asc/Sema/Sema.h"
#include "gtest/gtest.h"

using namespace asc;

// Helper: parse + analyze source.
struct SemaTestFixture {
  SourceManager sm;
  DiagnosticEngine *diags;
  ASTContext ctx;
  std::string output;
  llvm::raw_string_ostream *os;

  SemaTestFixture() {
    diags = nullptr;
    os = nullptr;
  }

  std::vector<Decl *> analyzeSource(llvm::StringRef source) {
    FileID fid = sm.createBuffer("test.ts", source);
    diags = new DiagnosticEngine(sm);
    os = new llvm::raw_string_ostream(output);
    diags->setOutputStream(*os);
    Lexer lexer(fid, sm, *diags);
    Parser parser(lexer, ctx, *diags);
    auto items = parser.parseProgram();
    Sema sema(ctx, *diags);
    sema.analyze(items);
    return items;
  }

  ~SemaTestFixture() {
    delete os;
    delete diags;
  }
};

TEST(SemaTest, SimpleFunction) {
  SemaTestFixture f;
  auto items = f.analyzeSource(
      "function main(): i32 { const x: i32 = 42; return x; }");
  EXPECT_FALSE(f.diags->hasErrors());
}

TEST(SemaTest, UndeclaredVariable) {
  SemaTestFixture f;
  f.analyzeSource(
      "function test(): i32 { return y; }");
  EXPECT_TRUE(f.diags->hasErrors());
}

TEST(SemaTest, DuplicateDeclaration) {
  SemaTestFixture f;
  f.analyzeSource(
      "function foo(): void {}\n"
      "function foo(): void {}");
  EXPECT_TRUE(f.diags->hasErrors());
}

TEST(SemaTest, TypeInference) {
  SemaTestFixture f;
  f.analyzeSource(
      "function test(): void { const x = 42; }");
  EXPECT_FALSE(f.diags->hasErrors());
}

TEST(SemaTest, StructRegistration) {
  SemaTestFixture f;
  f.analyzeSource(
      "struct Point { x: f64, y: f64 }\n"
      "function test(): void {}");
  EXPECT_FALSE(f.diags->hasErrors());
}

TEST(SemaTest, CopyAttributeCheck) {
  SemaTestFixture f;
  f.analyzeSource(
      "@copy struct Pair { x: i32, y: i32 }");
  EXPECT_FALSE(f.diags->hasErrors());
}

TEST(SemaTest, ImmutableAssignment) {
  SemaTestFixture f;
  f.analyzeSource(
      "function test(): void { const x: i32 = 1; x = 2; }");
  EXPECT_TRUE(f.diags->hasErrors());
}

TEST(SemaTest, MutableAssignment) {
  SemaTestFixture f;
  f.analyzeSource(
      "function test(): void { let x: i32 = 1; x = 2; }");
  EXPECT_FALSE(f.diags->hasErrors());
}

TEST(SemaTest, MultipleItems) {
  SemaTestFixture f;
  f.analyzeSource(
      "struct Foo { x: i32 }\n"
      "enum Bar { A, B }\n"
      "trait Baz { fn method(ref<Self>): void; }\n"
      "function main(): void {}");
  EXPECT_FALSE(f.diags->hasErrors());
}
