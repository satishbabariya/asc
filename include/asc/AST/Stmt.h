#ifndef ASC_AST_STMT_H
#define ASC_AST_STMT_H

#include "asc/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

namespace asc {

class Expr;
class Decl;
class VarDecl;

/// Discriminator for Stmt subclasses.
enum class StmtKind {
  Compound,
  Let,
  Const,
  Expression,
  Return,
  Break,
  Continue,
  Item,
};

/// Base class for all statements.
class Stmt {
public:
  virtual ~Stmt() = default;
  StmtKind getKind() const { return kind; }
  SourceLocation getLocation() const { return loc; }

protected:
  Stmt(StmtKind kind, SourceLocation loc) : kind(kind), loc(loc) {}

private:
  StmtKind kind;
  SourceLocation loc;
};

/// Block statement: `{ stmt* expr? }`
class CompoundStmt : public Stmt {
public:
  CompoundStmt(std::vector<Stmt *> stmts, Expr *trailingExpr,
               SourceLocation loc)
      : Stmt(StmtKind::Compound, loc), stmts(std::move(stmts)),
        trailingExpr(trailingExpr) {}

  const std::vector<Stmt *> &getStmts() const { return stmts; }
  Expr *getTrailingExpr() const { return trailingExpr; }

  static bool classof(const Stmt *s) {
    return s->getKind() == StmtKind::Compound;
  }

private:
  std::vector<Stmt *> stmts;
  Expr *trailingExpr;
};

/// `let x: T = expr;` or `let Pattern = expr else { diverge };`
class LetStmt : public Stmt {
public:
  LetStmt(VarDecl *decl, SourceLocation loc,
          CompoundStmt *elseBlock = nullptr)
      : Stmt(StmtKind::Let, loc), decl(decl), elseBlock(elseBlock) {}

  VarDecl *getDecl() const { return decl; }
  CompoundStmt *getElseBlock() const { return elseBlock; }
  bool hasElse() const { return elseBlock != nullptr; }

  static bool classof(const Stmt *s) { return s->getKind() == StmtKind::Let; }

private:
  VarDecl *decl;
  CompoundStmt *elseBlock;
};

class ConstStmt : public Stmt {
public:
  ConstStmt(VarDecl *decl, SourceLocation loc)
      : Stmt(StmtKind::Const, loc), decl(decl) {}

  VarDecl *getDecl() const { return decl; }

  static bool classof(const Stmt *s) {
    return s->getKind() == StmtKind::Const;
  }

private:
  VarDecl *decl;
};

/// Expression statement: `expr;`
class ExprStmt : public Stmt {
public:
  ExprStmt(Expr *expr, SourceLocation loc)
      : Stmt(StmtKind::Expression, loc), expr(expr) {}

  Expr *getExpr() const { return expr; }

  static bool classof(const Stmt *s) {
    return s->getKind() == StmtKind::Expression;
  }

private:
  Expr *expr;
};

/// `return expr?;`
class ReturnStmt : public Stmt {
public:
  ReturnStmt(Expr *value, SourceLocation loc)
      : Stmt(StmtKind::Return, loc), value(value) {}

  Expr *getValue() const { return value; }

  static bool classof(const Stmt *s) {
    return s->getKind() == StmtKind::Return;
  }

private:
  Expr *value;
};

class BreakStmt : public Stmt {
public:
  BreakStmt(Expr *value, std::string label, SourceLocation loc)
      : Stmt(StmtKind::Break, loc), value(value), label(std::move(label)) {}

  Expr *getValue() const { return value; }
  llvm::StringRef getLabel() const { return label; }

  static bool classof(const Stmt *s) {
    return s->getKind() == StmtKind::Break;
  }

private:
  Expr *value;
  std::string label;
};

class ContinueStmt : public Stmt {
public:
  ContinueStmt(std::string label, SourceLocation loc)
      : Stmt(StmtKind::Continue, loc), label(std::move(label)) {}

  llvm::StringRef getLabel() const { return label; }

  static bool classof(const Stmt *s) {
    return s->getKind() == StmtKind::Continue;
  }

private:
  std::string label;
};

/// A declaration used as a statement.
class ItemStmt : public Stmt {
public:
  ItemStmt(Decl *decl, SourceLocation loc)
      : Stmt(StmtKind::Item, loc), decl(decl) {}

  Decl *getDecl() const { return decl; }

  static bool classof(const Stmt *s) { return s->getKind() == StmtKind::Item; }

private:
  Decl *decl;
};

} // namespace asc

#endif // ASC_AST_STMT_H
