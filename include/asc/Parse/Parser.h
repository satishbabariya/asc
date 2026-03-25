#ifndef ASC_PARSE_PARSER_H
#define ASC_PARSE_PARSER_H

#include "asc/AST/ASTContext.h"
#include "asc/AST/Decl.h"
#include "asc/AST/Expr.h"
#include "asc/AST/Stmt.h"
#include "asc/AST/Type.h"
#include "asc/Basic/Diagnostic.h"
#include "asc/Lex/Lexer.h"
#include "asc/Lex/Token.h"
#include <vector>

namespace asc {

/// Recursive descent parser for AssemblyScript.
class Parser {
public:
  Parser(Lexer &lexer, ASTContext &ctx, DiagnosticEngine &diags);

  /// Parse the entire source file.
  std::vector<Decl *> parseProgram();

private:
  // --- Token management ---
  Token tok;       // current token
  void advance();  // consume current, get next
  bool expect(tok::TokenKind kind); // consume if match, else error
  bool consume(tok::TokenKind kind); // consume if match, return success
  bool check(tok::TokenKind kind) const;

  /// Skip tokens until a synchronization point.
  void skipToSync();

  /// Report an error diagnostic.
  void error(llvm::StringRef msg);
  void error(DiagID id, llvm::StringRef msg);

  // --- Declarations (ParseDecl.cpp) ---
  Decl *parseItem();
  FunctionDecl *parseFunctionDef();
  StructDecl *parseStructDef();
  EnumDecl *parseEnumDef();
  TraitDecl *parseTraitDef();
  ImplDecl *parseImplBlock();
  ImportDecl *parseImportDecl();
  ExportDecl *parseExportDecl();
  TypeAliasDecl *parseTypeAlias();
  ConstDecl *parseConstDef();
  StaticDecl *parseStaticDef();

  std::vector<ParamDecl> parseParamList();
  std::vector<GenericParam> parseGenericParams();
  std::vector<Type *> parseGenericArgs();
  std::vector<WhereConstraint> parseWhereClause();

  // --- Statements (ParseStmt.cpp) ---
  CompoundStmt *parseBlock();
  Stmt *parseStmt();
  Stmt *parseLetOrConstStmt();

  // --- Expressions (ParseExpr.cpp) ---
  Expr *parseExpr();
  Expr *parseAssignExpr();
  Expr *parseBinaryExpr(int minPrec);
  Expr *parseUnaryExpr();
  Expr *parsePostfixExpr();
  Expr *parsePrimaryExpr();
  Expr *parseIfExpr();
  Expr *parseMatchExpr();
  Expr *parseLoopExpr();
  Expr *parseWhileExpr();
  Expr *parseForExpr();
  Expr *parseClosureExpr();
  Expr *parseBlockExpr();
  Expr *parseMacroCallExpr(std::string name);

  std::vector<Expr *> parseArgList();

  // --- Types ---
  Type *parseType();
  Type *parsePrimaryType();

  // --- Patterns ---
  Pattern *parsePattern();

  // --- Operator precedence ---
  static int getPrecedence(tok::TokenKind kind);
  static BinaryOp toBinaryOp(tok::TokenKind kind);
  static bool isRightAssociative(tok::TokenKind kind);

  Lexer &lexer;
  ASTContext &ctx;
  DiagnosticEngine &diags;
};

} // namespace asc

#endif // ASC_PARSE_PARSER_H
