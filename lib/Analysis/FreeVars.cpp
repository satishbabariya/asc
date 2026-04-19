#include "asc/Analysis/FreeVars.h"
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
      for (auto *stmt : block->getBlock()->getStmts()) {
        if (auto *exprStmt = dynamic_cast<ExprStmt *>(stmt))
          collectFreeVars(exprStmt->getExpr(), boundNames, freeVars);
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
