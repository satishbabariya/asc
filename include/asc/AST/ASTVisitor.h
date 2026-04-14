#ifndef ASC_AST_ASTVISITOR_H
#define ASC_AST_ASTVISITOR_H

#include "asc/AST/Decl.h"
#include "asc/AST/Expr.h"
#include "asc/AST/Stmt.h"

namespace asc {

/// CRTP visitor for AST nodes. Override visit* methods in Derived.
template <typename Derived, typename RetTy = void>
class ASTVisitor {
public:
  Derived &getDerived() { return static_cast<Derived &>(*this); }

  // --- Decl dispatch ---
  RetTy visitDecl(Decl *d) {
    switch (d->getKind()) {
    case DeclKind::Function:
      return getDerived().visitFunctionDecl(static_cast<FunctionDecl *>(d));
    case DeclKind::Var:
      return getDerived().visitVarDecl(static_cast<VarDecl *>(d));
    case DeclKind::Struct:
      return getDerived().visitStructDecl(static_cast<StructDecl *>(d));
    case DeclKind::Enum:
      return getDerived().visitEnumDecl(static_cast<EnumDecl *>(d));
    case DeclKind::Trait:
      return getDerived().visitTraitDecl(static_cast<TraitDecl *>(d));
    case DeclKind::Impl:
      return getDerived().visitImplDecl(static_cast<ImplDecl *>(d));
    case DeclKind::TypeAlias:
      return getDerived().visitTypeAliasDecl(
          static_cast<TypeAliasDecl *>(d));
    case DeclKind::Import:
      return getDerived().visitImportDecl(static_cast<ImportDecl *>(d));
    case DeclKind::Export:
      return getDerived().visitExportDecl(static_cast<ExportDecl *>(d));
    case DeclKind::Field:
      return getDerived().visitFieldDecl(static_cast<FieldDecl *>(d));
    case DeclKind::EnumVariant:
      return getDerived().visitEnumVariantDecl(
          static_cast<EnumVariantDecl *>(d));
    case DeclKind::Const:
      return getDerived().visitConstDecl(static_cast<ConstDecl *>(d));
    case DeclKind::Static:
      return getDerived().visitStaticDecl(static_cast<StaticDecl *>(d));
    }
    return RetTy();
  }

  // --- Stmt dispatch ---
  RetTy visitStmt(Stmt *s) {
    switch (s->getKind()) {
    case StmtKind::Compound:
      return getDerived().visitCompoundStmt(static_cast<CompoundStmt *>(s));
    case StmtKind::Let:
      return getDerived().visitLetStmt(static_cast<LetStmt *>(s));
    case StmtKind::Const:
      return getDerived().visitConstStmt(static_cast<ConstStmt *>(s));
    case StmtKind::Expression:
      return getDerived().visitExprStmt(static_cast<ExprStmt *>(s));
    case StmtKind::Return:
      return getDerived().visitReturnStmt(static_cast<ReturnStmt *>(s));
    case StmtKind::Break:
      return getDerived().visitBreakStmt(static_cast<BreakStmt *>(s));
    case StmtKind::Continue:
      return getDerived().visitContinueStmt(static_cast<ContinueStmt *>(s));
    case StmtKind::Item:
      return getDerived().visitItemStmt(static_cast<ItemStmt *>(s));
    }
    return RetTy();
  }

  // --- Expr dispatch ---
  RetTy visitExpr(Expr *e) {
    switch (e->getKind()) {
    case ExprKind::IntegerLiteral:
      return getDerived().visitIntegerLiteral(
          static_cast<IntegerLiteral *>(e));
    case ExprKind::FloatLiteral:
      return getDerived().visitFloatLiteral(
          static_cast<FloatLiteral *>(e));
    case ExprKind::StringLiteral:
      return getDerived().visitStringLiteral(
          static_cast<StringLiteral *>(e));
    case ExprKind::CharLiteral:
      return getDerived().visitCharLiteral(static_cast<CharLiteral *>(e));
    case ExprKind::BoolLiteral:
      return getDerived().visitBoolLiteral(static_cast<BoolLiteral *>(e));
    case ExprKind::NullLiteral:
      return getDerived().visitNullLiteral(static_cast<NullLiteral *>(e));
    case ExprKind::ArrayLiteral:
      return getDerived().visitArrayLiteral(
          static_cast<ArrayLiteral *>(e));
    case ExprKind::ArrayRepeat:
      return getDerived().visitArrayRepeatExpr(
          static_cast<ArrayRepeatExpr *>(e));
    case ExprKind::StructLiteral:
      return getDerived().visitStructLiteral(
          static_cast<StructLiteral *>(e));
    case ExprKind::TupleLiteral:
      return getDerived().visitTupleLiteral(
          static_cast<TupleLiteral *>(e));
    case ExprKind::DeclRef:
      return getDerived().visitDeclRefExpr(static_cast<DeclRefExpr *>(e));
    case ExprKind::Binary:
      return getDerived().visitBinaryExpr(static_cast<BinaryExpr *>(e));
    case ExprKind::Unary:
      return getDerived().visitUnaryExpr(static_cast<UnaryExpr *>(e));
    case ExprKind::Call:
      return getDerived().visitCallExpr(static_cast<CallExpr *>(e));
    case ExprKind::MethodCall:
      return getDerived().visitMethodCallExpr(
          static_cast<MethodCallExpr *>(e));
    case ExprKind::FieldAccess:
      return getDerived().visitFieldAccessExpr(
          static_cast<FieldAccessExpr *>(e));
    case ExprKind::Index:
      return getDerived().visitIndexExpr(static_cast<IndexExpr *>(e));
    case ExprKind::Cast:
      return getDerived().visitCastExpr(static_cast<CastExpr *>(e));
    case ExprKind::Range:
      return getDerived().visitRangeExpr(static_cast<RangeExpr *>(e));
    case ExprKind::If:
      return getDerived().visitIfExpr(static_cast<IfExpr *>(e));
    case ExprKind::IfLet:
      return getDerived().visitIfLetExpr(static_cast<IfLetExpr *>(e));
    case ExprKind::Match:
      return getDerived().visitMatchExpr(static_cast<MatchExpr *>(e));
    case ExprKind::Loop:
      return getDerived().visitLoopExpr(static_cast<LoopExpr *>(e));
    case ExprKind::While:
      return getDerived().visitWhileExpr(static_cast<WhileExpr *>(e));
    case ExprKind::WhileLet:
      return getDerived().visitWhileLetExpr(static_cast<WhileLetExpr *>(e));
    case ExprKind::For:
      return getDerived().visitForExpr(static_cast<ForExpr *>(e));
    case ExprKind::Closure:
      return getDerived().visitClosureExpr(static_cast<ClosureExpr *>(e));
    case ExprKind::Assign:
      return getDerived().visitAssignExpr(static_cast<AssignExpr *>(e));
    case ExprKind::Block:
      return getDerived().visitBlockExpr(static_cast<BlockExpr *>(e));
    case ExprKind::MacroCall:
      return getDerived().visitMacroCallExpr(
          static_cast<MacroCallExpr *>(e));
    case ExprKind::UnsafeBlock:
      return getDerived().visitUnsafeBlockExpr(
          static_cast<UnsafeBlockExpr *>(e));
    case ExprKind::TemplateLiteral:
      return getDerived().visitTemplateLiteralExpr(
          static_cast<TemplateLiteralExpr *>(e));
    case ExprKind::Try:
      return getDerived().visitTryExpr(static_cast<TryExpr *>(e));
    case ExprKind::Path:
      return getDerived().visitPathExpr(static_cast<PathExpr *>(e));
    case ExprKind::Paren:
      return getDerived().visitParenExpr(static_cast<ParenExpr *>(e));
    case ExprKind::TaskScope:
      return getDerived().visitTaskScopeExpr(static_cast<TaskScopeExpr *>(e));
    }
    return RetTy();
  }

  // Default implementations — override in Derived.
#define DEFAULT_VISIT(Name) \
  RetTy visit##Name(Name *) { return RetTy(); }

  DEFAULT_VISIT(FunctionDecl)
  DEFAULT_VISIT(VarDecl)
  DEFAULT_VISIT(StructDecl)
  DEFAULT_VISIT(EnumDecl)
  DEFAULT_VISIT(TraitDecl)
  DEFAULT_VISIT(ImplDecl)
  DEFAULT_VISIT(TypeAliasDecl)
  DEFAULT_VISIT(ImportDecl)
  DEFAULT_VISIT(ExportDecl)
  DEFAULT_VISIT(FieldDecl)
  DEFAULT_VISIT(EnumVariantDecl)
  DEFAULT_VISIT(ConstDecl)
  DEFAULT_VISIT(StaticDecl)

  DEFAULT_VISIT(CompoundStmt)
  DEFAULT_VISIT(LetStmt)
  DEFAULT_VISIT(ConstStmt)
  DEFAULT_VISIT(ExprStmt)
  DEFAULT_VISIT(ReturnStmt)
  DEFAULT_VISIT(BreakStmt)
  DEFAULT_VISIT(ContinueStmt)
  DEFAULT_VISIT(ItemStmt)

  DEFAULT_VISIT(IntegerLiteral)
  DEFAULT_VISIT(FloatLiteral)
  DEFAULT_VISIT(StringLiteral)
  DEFAULT_VISIT(CharLiteral)
  DEFAULT_VISIT(BoolLiteral)
  DEFAULT_VISIT(NullLiteral)
  DEFAULT_VISIT(ArrayLiteral)
  DEFAULT_VISIT(ArrayRepeatExpr)
  DEFAULT_VISIT(StructLiteral)
  DEFAULT_VISIT(TupleLiteral)
  DEFAULT_VISIT(DeclRefExpr)
  DEFAULT_VISIT(BinaryExpr)
  DEFAULT_VISIT(UnaryExpr)
  DEFAULT_VISIT(CallExpr)
  DEFAULT_VISIT(MethodCallExpr)
  DEFAULT_VISIT(FieldAccessExpr)
  DEFAULT_VISIT(IndexExpr)
  DEFAULT_VISIT(CastExpr)
  DEFAULT_VISIT(RangeExpr)
  DEFAULT_VISIT(IfExpr)
  DEFAULT_VISIT(IfLetExpr)
  DEFAULT_VISIT(MatchExpr)
  DEFAULT_VISIT(LoopExpr)
  DEFAULT_VISIT(WhileExpr)
  DEFAULT_VISIT(WhileLetExpr)
  DEFAULT_VISIT(ForExpr)
  DEFAULT_VISIT(ClosureExpr)
  DEFAULT_VISIT(AssignExpr)
  DEFAULT_VISIT(BlockExpr)
  DEFAULT_VISIT(MacroCallExpr)
  DEFAULT_VISIT(UnsafeBlockExpr)
  DEFAULT_VISIT(TemplateLiteralExpr)
  DEFAULT_VISIT(TryExpr)
  DEFAULT_VISIT(PathExpr)
  DEFAULT_VISIT(ParenExpr)
  DEFAULT_VISIT(TaskScopeExpr)

#undef DEFAULT_VISIT
};

} // namespace asc

#endif // ASC_AST_ASTVISITOR_H
