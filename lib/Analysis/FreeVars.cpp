#include "asc/Analysis/FreeVars.h"
#include "asc/AST/Decl.h"
#include "asc/AST/Expr.h"
#include "asc/AST/Stmt.h"

namespace asc {

// Walk a CompoundStmt (block): growing `localBound` with names introduced by
// LetStmt as we go, recursing into ExprStmt / ReturnStmt operands, and
// visiting the trailing expression (the final expression-as-value) if any.
//
// `localBound` is taken by value so the caller's scope is not polluted with
// inner block bindings, but the caller can pass in their own bound set to
// seed the walk (e.g. closure parameter names or an enclosing scope).
static void walkBlock(const CompoundStmt *block, llvm::StringSet<> localBound,
                      llvm::StringSet<> &freeVars) {
  if (!block)
    return;
  for (auto *stmt : block->getStmts()) {
    if (auto *ls = dynamic_cast<const LetStmt *>(stmt)) {
      // Initializer is evaluated before the new name comes into scope.
      if (auto *decl = ls->getDecl()) {
        if (decl->getInit())
          collectFreeVars(decl->getInit(), localBound, freeVars);
        if (!decl->getPattern() && !decl->getName().empty())
          localBound.insert(decl->getName());
      }
    } else if (auto *exprStmt = dynamic_cast<const ExprStmt *>(stmt)) {
      collectFreeVars(exprStmt->getExpr(), localBound, freeVars);
    } else if (auto *rs = dynamic_cast<const ReturnStmt *>(stmt)) {
      // `return expr;` — must visit operand so it's collected as a free
      // variable (Send validation / env-struct synthesis depend on it).
      if (rs->getValue())
        collectFreeVars(rs->getValue(), localBound, freeVars);
    }
  }
  // Trailing expression: `{ ...; x }` — the x must be walked too.
  if (block->getTrailingExpr())
    collectFreeVars(block->getTrailingExpr(), localBound, freeVars);
}

// Collect all DeclRefExpr names from an expression tree.
void collectFreeVars(Expr *expr, const llvm::StringSet<> &boundNames,
                     llvm::StringSet<> &freeVars) {
  if (!expr)
    return;
  if (auto *ref = dynamic_cast<DeclRefExpr *>(expr)) {
    if (!boundNames.contains(ref->getName()))
      freeVars.insert(ref->getName());
    return;
  }
  if (auto *bin = dynamic_cast<BinaryExpr *>(expr)) {
    collectFreeVars(bin->getLHS(), boundNames, freeVars);
    collectFreeVars(bin->getRHS(), boundNames, freeVars);
    return;
  }
  if (auto *un = dynamic_cast<UnaryExpr *>(expr)) {
    collectFreeVars(un->getOperand(), boundNames, freeVars);
    return;
  }
  if (auto *call = dynamic_cast<CallExpr *>(expr)) {
    collectFreeVars(call->getCallee(), boundNames, freeVars);
    for (auto *arg : call->getArgs())
      collectFreeVars(arg, boundNames, freeVars);
    return;
  }
  if (auto *ifE = dynamic_cast<IfExpr *>(expr)) {
    collectFreeVars(ifE->getCondition(), boundNames, freeVars);
    walkBlock(ifE->getThenBlock(), boundNames, freeVars);
    // Else branch can be a CompoundStmt block or an ExprStmt wrapping
    // another IfExpr (else-if chain). The latter is routed back through
    // collectFreeVars so the inner IfExpr's condition / then / else are
    // all traversed.
    if (auto *elseStmt = ifE->getElseBlock()) {
      if (auto *elseBlock = dynamic_cast<CompoundStmt *>(elseStmt)) {
        walkBlock(elseBlock, boundNames, freeVars);
      } else if (auto *elseExprStmt = dynamic_cast<ExprStmt *>(elseStmt)) {
        collectFreeVars(elseExprStmt->getExpr(), boundNames, freeVars);
      }
    }
    return;
  }
  if (auto *block = dynamic_cast<BlockExpr *>(expr)) {
    // A block introduces a scope: walk statements in order, growing a
    // local bound-name set as `let` bindings are encountered so that
    // subsequent references to those names are NOT treated as free.
    // Note: destructuring `let (a, b) = ...` patterns are not yet
    // handled; only the common single-identifier case.
    walkBlock(block->getBlock(), boundNames, freeVars);
    return;
  }
  if (auto *paren = dynamic_cast<ParenExpr *>(expr)) {
    collectFreeVars(paren->getInner(), boundNames, freeVars);
    return;
  }
  if (auto *ts = dynamic_cast<TaskScopeExpr *>(expr)) {
    walkBlock(ts->getBody(), boundNames, freeVars);
    return;
  }
  // For other expression types, the current level of capture analysis
  // is sufficient — if more complex closures are needed, extend here.
}

} // namespace asc
