#ifndef ASC_HIR_HIRBUILDER_H
#define ASC_HIR_HIRBUILDER_H

#include "asc/AST/ASTContext.h"
#include "asc/AST/ASTVisitor.h"
#include "asc/AST/Decl.h"
#include "asc/AST/Expr.h"
#include "asc/AST/Stmt.h"
#include "asc/AST/Type.h"
#include "asc/HIR/OwnDialect.h"
#include "asc/HIR/OwnOps.h"
#include "asc/HIR/OwnTypes.h"
#include "asc/HIR/TaskDialect.h"
#include "asc/HIR/TaskOps.h"
#include "asc/Sema/Sema.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/StringMap.h"
#include <string>
#include <vector>

namespace asc {

//===----------------------------------------------------------------------===//
// HIRBuilder
//
// Translates a type-checked AST into MLIR using the own and task dialects.
// This is the "AST lowering" pass — analogous to Flang's FIR emission from
// PFT, or Clang's CodeGenFunction from AST.
//
// Usage:
//   HIRBuilder builder(mlirCtx, astCtx, sema);
//   mlir::OwningOpRef<mlir::ModuleOp> mod = builder.buildModule(topLevelDecls);
//===----------------------------------------------------------------------===//
class HIRBuilder : public ASTVisitor<HIRBuilder, mlir::Value> {
public:
  HIRBuilder(mlir::MLIRContext &mlirCtx, ASTContext &astCtx, Sema &sema,
             const SourceManager &sm);

  /// Build an MLIR module from top-level declarations.
  mlir::OwningOpRef<mlir::ModuleOp>
  buildModule(const std::vector<Decl *> &decls);

  // ----- Decl visitors (return nullptr since decls emit into module) -----
  mlir::Value visitFunctionDecl(FunctionDecl *d);
  mlir::Value visitStructDecl(StructDecl *d);
  mlir::Value visitVarDecl(VarDecl *d);
  mlir::Value visitConstDecl(ConstDecl *d);
  mlir::Value visitStaticDecl(StaticDecl *d);
  mlir::Value visitImportDecl(ImportDecl *d);
  mlir::Value visitExportDecl(ExportDecl *d);
  mlir::Value visitEnumDecl(EnumDecl *d);
  mlir::Value visitTraitDecl(TraitDecl *d);
  mlir::Value visitImplDecl(ImplDecl *d);
  mlir::Value visitTypeAliasDecl(TypeAliasDecl *d);
  mlir::Value visitFieldDecl(FieldDecl *d);
  mlir::Value visitEnumVariantDecl(EnumVariantDecl *d);

  // ----- Stmt visitors -----
  mlir::Value visitCompoundStmt(CompoundStmt *s);
  mlir::Value visitLetStmt(LetStmt *s);
  mlir::Value visitConstStmt(ConstStmt *s);
  mlir::Value visitExprStmt(ExprStmt *s);
  mlir::Value visitReturnStmt(ReturnStmt *s);
  mlir::Value visitBreakStmt(BreakStmt *s);
  mlir::Value visitContinueStmt(ContinueStmt *s);
  mlir::Value visitItemStmt(ItemStmt *s);

  // ----- Expr visitors -----
  mlir::Value visitIntegerLiteral(IntegerLiteral *e);
  mlir::Value visitFloatLiteral(FloatLiteral *e);
  mlir::Value visitStringLiteral(StringLiteral *e);
  mlir::Value visitBoolLiteral(BoolLiteral *e);
  mlir::Value visitNullLiteral(NullLiteral *e);
  mlir::Value visitDeclRefExpr(DeclRefExpr *e);
  mlir::Value visitBinaryExpr(BinaryExpr *e);
  mlir::Value visitUnaryExpr(UnaryExpr *e);
  mlir::Value visitCallExpr(CallExpr *e);
  mlir::Value visitIfExpr(IfExpr *e);
  mlir::Value visitIfLetExpr(IfLetExpr *e);
  mlir::Value visitBlockExpr(BlockExpr *e);
  mlir::Value visitAssignExpr(AssignExpr *e);
  mlir::Value visitArrayLiteral(ArrayLiteral *e);
  mlir::Value visitStructLiteral(StructLiteral *e);
  mlir::Value visitTupleLiteral(TupleLiteral *e);
  mlir::Value visitMethodCallExpr(MethodCallExpr *e);
  mlir::Value visitFieldAccessExpr(FieldAccessExpr *e);
  mlir::Value visitIndexExpr(IndexExpr *e);
  mlir::Value visitCastExpr(CastExpr *e);
  mlir::Value visitClosureExpr(ClosureExpr *e);
  mlir::Value visitMatchExpr(MatchExpr *e);
  mlir::Value visitLoopExpr(LoopExpr *e);
  mlir::Value visitWhileExpr(WhileExpr *e);
  mlir::Value visitForExpr(ForExpr *e);
  mlir::Value visitRangeExpr(RangeExpr *e);
  mlir::Value visitCharLiteral(CharLiteral *e);
  mlir::Value visitArrayRepeatExpr(ArrayRepeatExpr *e);
  mlir::Value visitMacroCallExpr(MacroCallExpr *e);
  mlir::Value visitUnsafeBlockExpr(UnsafeBlockExpr *e);
  mlir::Value visitTemplateLiteralExpr(TemplateLiteralExpr *e);
  mlir::Value visitTryExpr(TryExpr *e);
  mlir::Value visitPathExpr(PathExpr *e);
  mlir::Value visitParenExpr(ParenExpr *e);

private:
  /// Convert an AST Type to an MLIR Type.
  mlir::Type convertType(asc::Type *astType);

  /// Convert a builtin type kind to an MLIR integer/float type.
  mlir::Type convertBuiltinType(BuiltinTypeKind kind);

  /// Create an MLIR location from an AST SourceLocation.
  mlir::Location loc(SourceLocation astLoc);

  /// Declare a variable in the current scope's symbol table.
  void declare(llvm::StringRef name, mlir::Value value);

  /// Look up a variable by name.
  mlir::Value lookup(llvm::StringRef name);

  /// Push/pop a variable scope.
  void pushScope();
  void popScope();

  /// Emit an own.alloc for a new variable binding.
  mlir::Value emitAlloc(mlir::Type type, mlir::Value init,
                        mlir::Location location);

  /// Emit an own.move for ownership transfer.
  mlir::Value emitMove(mlir::Value source, mlir::Location location);

  /// Emit an own.drop for a value going out of scope.
  void emitDrop(mlir::Value value, mlir::Location location);

  /// Emit own.borrow_ref for a shared borrow.
  mlir::Value emitBorrowRef(mlir::Value owned, mlir::Location location);

  /// Emit own.borrow_mut for a mutable borrow.
  mlir::Value emitBorrowMut(mlir::Value owned, mlir::Location location);

  /// Determine whether an AST type should produce an owned MLIR type.
  bool isOwnedType(asc::Type *astType);

  /// Emit a function body.
  void emitFunctionBody(FunctionDecl *d, mlir::func::FuncOp funcOp);

  // ---- State ----
  mlir::MLIRContext &mlirCtx;
  ASTContext &astCtx;
  Sema &sema;
  const SourceManager &sourceManager;
  mlir::OpBuilder builder;
  mlir::ModuleOp module;

  /// Scoped symbol table: maps variable names to MLIR Values.
  using SymbolTable = llvm::ScopedHashTable<llvm::StringRef, mlir::Value>;
  using SymbolScope = llvm::ScopedHashTableScope<llvm::StringRef, mlir::Value>;
  SymbolTable symbolTable;

  /// Stack of active scopes (for managing ScopedHashTable lifetimes).
  std::vector<std::unique_ptr<SymbolScope>> scopeStack;

  /// Current function being built (for return type info).
  mlir::func::FuncOp currentFunction;

  /// Loop context for break/continue support.
  struct LoopContext {
    mlir::Block *continueBlock; // target for 'continue' (condBlock or bodyBlock)
    mlir::Block *exitBlock;     // target for 'break'
  };
  std::vector<LoopContext> loopStack;

  /// Active type parameter substitutions for generic monomorphization.
  llvm::StringMap<mlir::Type> typeSubstitutions;

  /// Cache of MLIR struct types by name to avoid re-creation.
  llvm::StringMap<mlir::Type> structTypeCache;

  /// Convert a struct declaration to an LLVM struct type.
  mlir::Type convertStructType(StructDecl *sd);

  /// Get the LLVM pointer type.
  mlir::Type getPtrType();

  /// Get the size of a type in bytes for the target.
  uint64_t getTypeSize(mlir::Type type);

  /// Get the LLVM tagged union struct type for an enum (for GEP indexing).
  mlir::Type getEnumStructType(llvm::StringRef enumName);
};

} // namespace asc

#endif // ASC_HIR_HIRBUILDER_H
