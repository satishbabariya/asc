#ifndef ASC_SEMA_SEMA_H
#define ASC_SEMA_SEMA_H

#include "asc/AST/ASTContext.h"
#include "asc/AST/Decl.h"
#include "asc/AST/Expr.h"
#include "asc/AST/Stmt.h"
#include "asc/AST/Type.h"
#include "asc/Basic/Diagnostic.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include <string>
#include <vector>

namespace asc {

/// Symbol table entry.
struct Symbol {
  std::string name;
  Decl *decl = nullptr;
  Type *type = nullptr;
  bool isMutable = false;
};

/// Lexical scope for name resolution.
class Scope {
public:
  explicit Scope(Scope *parent = nullptr) : parent(parent) {}

  bool declare(llvm::StringRef name, Symbol sym);
  Symbol *lookup(llvm::StringRef name);
  Symbol *lookupLocal(llvm::StringRef name);
  Scope *getParent() const { return parent; }

private:
  Scope *parent;
  llvm::StringMap<Symbol> symbols;
};

/// Semantic analysis: name resolution, type checking, ownership inference.
class Sema {
public:
  Sema(ASTContext &ctx, DiagnosticEngine &diags);

  /// Analyze all top-level declarations.
  void analyze(std::vector<Decl *> &items);

  /// Check if analysis found errors.
  bool hasErrors() const { return diags.hasErrors(); }

private:
  // --- Declaration checking (SemaDecl.cpp) ---
  void checkDecl(Decl *d);
  void checkFunctionDecl(FunctionDecl *d);
  void checkStructDecl(StructDecl *d);
  void checkEnumDecl(EnumDecl *d);
  void checkTraitDecl(TraitDecl *d);
  void checkImplDecl(ImplDecl *d);
  void checkVarDecl(VarDecl *d);

  // --- Expression checking (SemaExpr.cpp) ---
  Type *checkExpr(Expr *e);
  Type *checkIntegerLiteral(IntegerLiteral *e);
  Type *checkFloatLiteral(FloatLiteral *e);
  Type *checkStringLiteral(StringLiteral *e);
  Type *checkBoolLiteral(BoolLiteral *e);
  Type *checkDeclRefExpr(DeclRefExpr *e);
  Type *checkBinaryExpr(BinaryExpr *e);
  Type *checkUnaryExpr(UnaryExpr *e);
  Type *checkCallExpr(CallExpr *e);
  Type *checkIfExpr(IfExpr *e);
  Type *checkBlockExpr(BlockExpr *e);
  Type *checkAssignExpr(AssignExpr *e);

  // --- Statement checking ---
  void checkStmt(Stmt *s);
  void checkCompoundStmt(CompoundStmt *s);
  void checkReturnStmt(ReturnStmt *s);

  // --- Type checking (SemaType.cpp) ---
  Type *resolveType(Type *t);
  bool isCompatible(Type *lhs, Type *rhs);
  bool isCopyType(Type *t);
  bool isSendType(Type *t);
  bool isSyncType(Type *t);
  void rejectUnsupportedFeature(llvm::StringRef feature, SourceLocation loc);

  // --- Scope management ---
  void pushScope();
  void popScope();

  ASTContext &ctx;
  DiagnosticEngine &diags;
  Scope *currentScope = nullptr;
  std::vector<std::unique_ptr<Scope>> scopes;
  Type *currentReturnType = nullptr;

  // Type registration for struct/enum/trait lookup.
  llvm::StringMap<StructDecl *> structDecls;
  llvm::StringMap<EnumDecl *> enumDecls;
  llvm::StringMap<TraitDecl *> traitDecls;
};

} // namespace asc

#endif // ASC_SEMA_SEMA_H
