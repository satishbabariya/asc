#include "asc/Analysis/FreeVars.h"
#include "asc/AST/Decl.h"
#include "asc/AST/Expr.h"
#include "asc/AST/Stmt.h"

namespace asc {

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
    if (ifE->getThenBlock()) {
      for (auto *stmt : ifE->getThenBlock()->getStmts()) {
        if (auto *exprStmt = dynamic_cast<ExprStmt *>(stmt))
          collectFreeVars(exprStmt->getExpr(), boundNames, freeVars);
      }
    }
    return;
  }
  if (auto *block = dynamic_cast<BlockExpr *>(expr)) {
    if (block->getBlock()) {
      // A block introduces a scope: walk statements in order, growing a
      // local bound-name set as `let` bindings are encountered so that
      // subsequent references to those names are NOT treated as free.
      // Note: destructuring `let (a, b) = ...` patterns are not yet
      // handled; only the common single-identifier case.
      llvm::StringSet<> localBound;
      for (const auto &entry : boundNames)
        localBound.insert(entry.getKey());
      for (auto *stmt : block->getBlock()->getStmts()) {
        if (auto *ls = dynamic_cast<LetStmt *>(stmt)) {
          // The initializer is evaluated before the binding comes into
          // scope, so collect its free vars against the *current*
          // localBound (excluding the new name).
          if (auto *decl = ls->getDecl()) {
            if (decl->getInit())
              collectFreeVars(decl->getInit(), localBound, freeVars);
            // Bind the declared name for subsequent statements. Skip
            // destructuring patterns for now (handled separately).
            if (!decl->getPattern() && !decl->getName().empty())
              localBound.insert(decl->getName());
          }
        } else if (auto *exprStmt = dynamic_cast<ExprStmt *>(stmt)) {
          collectFreeVars(exprStmt->getExpr(), localBound, freeVars);
        }
      }
    }
    return;
  }
  if (auto *paren = dynamic_cast<ParenExpr *>(expr)) {
    collectFreeVars(paren->getInner(), boundNames, freeVars);
    return;
  }
  if (auto *ts = dynamic_cast<TaskScopeExpr *>(expr)) {
    if (ts->getBody()) {
      for (auto *stmt : ts->getBody()->getStmts()) {
        if (auto *exprStmt = dynamic_cast<ExprStmt *>(stmt))
          collectFreeVars(exprStmt->getExpr(), boundNames, freeVars);
      }
    }
    return;
  }
  // For other expression types, the current level of capture analysis
  // is sufficient — if more complex closures are needed, extend here.
}

} // namespace asc
