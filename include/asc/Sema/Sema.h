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

/// Ownership classification for values and expressions.
enum class OwnershipKind {
  Owned,       // sole owner — must be moved or dropped
  Borrowed,    // shared immutable borrow (ref<T>)
  BorrowedMut, // exclusive mutable borrow (refmut<T>)
  Moved,       // ownership transferred to another binding/call
  Copied,      // @copy type — no ownership tracking
  Unknown,     // not yet inferred
};

/// Ownership metadata attached to expressions and variables.
struct OwnershipInfo {
  OwnershipKind kind = OwnershipKind::Unknown;
  bool isSend = false;
  bool isSync = false;
  bool isCopy = false;
};

/// Symbol table entry.
struct Symbol {
  std::string name;
  Decl *decl = nullptr;
  Type *type = nullptr;
  bool isMutable = false;
  bool isMoved = false;               // Track use-after-move
  SourceLocation movedAt;              // Where it was moved
  OwnershipInfo ownership;
};

/// Lexical scope for name resolution.
class Scope {
public:
  explicit Scope(Scope *parent = nullptr) : parent(parent) {}

  bool declare(llvm::StringRef name, Symbol sym);
  Symbol *lookup(llvm::StringRef name);
  Symbol *lookupLocal(llvm::StringRef name);
  Scope *getParent() const { return parent; }

  /// Iterate all symbols in this scope (for move state snapshots).
  void forEachSymbol(llvm::function_ref<void(llvm::StringRef, Symbol &)> fn) {
    for (auto &kv : symbols)
      fn(kv.first(), kv.second);
  }

private:
  Scope *parent;
  llvm::StringMap<Symbol> symbols;
};

/// Register built-in types and traits in Sema scope.
void registerBuiltins(ASTContext &ctx, Scope *scope,
                      llvm::StringMap<StructDecl *> &structDecls,
                      llvm::StringMap<EnumDecl *> &enumDecls,
                      llvm::StringMap<TraitDecl *> &traitDecls);

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
  Type *checkMethodCallExpr(MethodCallExpr *e);
  Type *checkFieldAccessExpr(FieldAccessExpr *e);
  Type *checkIndexExpr(IndexExpr *e);
  Type *checkMatchExpr(MatchExpr *e);
  Type *checkForExpr(ForExpr *e);
  Type *checkWhileExpr(WhileExpr *e);
  Type *checkLoopExpr(LoopExpr *e);
  Type *checkClosureExpr(ClosureExpr *e);
  Type *checkRangeExpr(RangeExpr *e);
  Type *checkCastExpr(CastExpr *e);
  Type *checkStructLiteral(StructLiteral *e);
  Type *checkTupleLiteral(TupleLiteral *e);
  Type *checkArrayLiteral(ArrayLiteral *e);
  Type *checkArrayRepeatExpr(ArrayRepeatExpr *e);
  Type *checkMacroCallExpr(MacroCallExpr *e);
  Type *checkTryExpr(TryExpr *e);
  Type *checkPathExpr(PathExpr *e);
  Type *checkTemplateLiteralExpr(TemplateLiteralExpr *e);

  // --- Ownership inference (SemaExpr.cpp) ---
  void inferCallOwnership(CallExpr *e, FunctionDecl *callee);
  OwnershipKind inferParamOwnership(Type *paramType);
  void markExprOwnership(Expr *e, OwnershipKind kind);

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

public:
  // Type registration for struct/enum/trait lookup (public for HIR builder).
  llvm::StringMap<StructDecl *> structDecls;
  llvm::StringMap<EnumDecl *> enumDecls;
  llvm::StringMap<TraitDecl *> traitDecls;
  llvm::StringMap<Type *> typeAliases;
private:

  // Impl blocks indexed by target type name.
  llvm::StringMap<llvm::SmallVector<ImplDecl *, 2>> implDecls;

  // Generic monomorphization cache: mangled name → instantiated decl.
  llvm::StringMap<Decl *> monoCache;

  // Monomorphize a generic type with concrete arguments.
  Type *monomorphizeType(llvm::StringRef baseName,
                         const std::vector<Type *> &typeArgs);
  std::string mangleGenericName(llvm::StringRef base,
                                const std::vector<Type *> &args);
  std::string mangleTypeName(Type *t);

  // Ownership tracking for expressions and variables.
  llvm::DenseMap<Expr *, OwnershipInfo> exprOwnership;
  llvm::DenseMap<VarDecl *, OwnershipInfo> varOwnership;

public:
  /// Get ownership info for an expression (used by HIR Builder).
  OwnershipInfo getExprOwnership(Expr *e) const {
    auto it = exprOwnership.find(e);
    return it != exprOwnership.end() ? it->second : OwnershipInfo{};
  }
  /// Get ownership info for a variable.
  OwnershipInfo getVarOwnership(VarDecl *d) const {
    auto it = varOwnership.find(d);
    return it != varOwnership.end() ? it->second : OwnershipInfo{};
  }
};

} // namespace asc

#endif // ASC_SEMA_SEMA_H
