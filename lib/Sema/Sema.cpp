#include "asc/Sema/Sema.h"

namespace asc {

// --- Scope ---

bool Scope::declare(llvm::StringRef name, Symbol sym) {
  auto [it, inserted] = symbols.try_emplace(name, std::move(sym));
  return inserted;
}

Symbol *Scope::lookup(llvm::StringRef name) {
  auto it = symbols.find(name);
  if (it != symbols.end())
    return &it->second;
  if (parent)
    return parent->lookup(name);
  return nullptr;
}

Symbol *Scope::lookupLocal(llvm::StringRef name) {
  auto it = symbols.find(name);
  if (it != symbols.end())
    return &it->second;
  return nullptr;
}

// --- Sema ---

Sema::Sema(ASTContext &ctx, DiagnosticEngine &diags)
    : ctx(ctx), diags(diags) {
  pushScope(); // global scope
  registerBuiltins(ctx, currentScope, structDecls, enumDecls);
}

void Sema::pushScope() {
  auto scope = std::make_unique<Scope>(currentScope);
  currentScope = scope.get();
  scopes.push_back(std::move(scope));
}

void Sema::popScope() {
  if (currentScope)
    currentScope = currentScope->getParent();
}

void Sema::analyze(std::vector<Decl *> &items) {
  // First pass: register all type declarations and impl blocks.
  for (auto *item : items) {
    if (auto *sd = dynamic_cast<StructDecl *>(item))
      structDecls[sd->getName()] = sd;
    else if (auto *ed = dynamic_cast<EnumDecl *>(item))
      enumDecls[ed->getName()] = ed;
    else if (auto *td = dynamic_cast<TraitDecl *>(item))
      traitDecls[td->getName()] = td;
    else if (auto *id = dynamic_cast<ImplDecl *>(item)) {
      if (auto *nt = dynamic_cast<NamedType *>(id->getTargetType()))
        implDecls[nt->getName()].push_back(id);
    }
    // Register exported items' inner declarations.
    else if (auto *ed = dynamic_cast<ExportDecl *>(item)) {
      if (auto *inner = ed->getInner()) {
        if (auto *sd = dynamic_cast<StructDecl *>(inner))
          structDecls[sd->getName()] = sd;
        else if (auto *enm = dynamic_cast<EnumDecl *>(inner))
          enumDecls[enm->getName()] = enm;
      }
    }
  }

  // Second pass: check all declarations.
  for (auto *item : items)
    checkDecl(item);
}

void Sema::checkDecl(Decl *d) {
  switch (d->getKind()) {
  case DeclKind::Function:
    checkFunctionDecl(static_cast<FunctionDecl *>(d));
    break;
  case DeclKind::Struct:
    checkStructDecl(static_cast<StructDecl *>(d));
    break;
  case DeclKind::Enum:
    checkEnumDecl(static_cast<EnumDecl *>(d));
    break;
  case DeclKind::Trait:
    checkTraitDecl(static_cast<TraitDecl *>(d));
    break;
  case DeclKind::Impl:
    checkImplDecl(static_cast<ImplDecl *>(d));
    break;
  case DeclKind::Var:
    checkVarDecl(static_cast<VarDecl *>(d));
    break;
  case DeclKind::Const:
  case DeclKind::Static:
  case DeclKind::TypeAlias:
  case DeclKind::Import:
  case DeclKind::Export:
  case DeclKind::Field:
  case DeclKind::EnumVariant:
    // Basic registration, no deep checking needed yet.
    break;
  }
}

void Sema::checkStmt(Stmt *s) {
  switch (s->getKind()) {
  case StmtKind::Compound:
    checkCompoundStmt(static_cast<CompoundStmt *>(s));
    break;
  case StmtKind::Return:
    checkReturnStmt(static_cast<ReturnStmt *>(s));
    break;
  case StmtKind::Let:
  case StmtKind::Const: {
    VarDecl *decl = nullptr;
    if (auto *ls = dynamic_cast<LetStmt *>(s))
      decl = ls->getDecl();
    else if (auto *cs = dynamic_cast<ConstStmt *>(s))
      decl = cs->getDecl();
    if (decl)
      checkVarDecl(decl);
    break;
  }
  case StmtKind::Expression: {
    auto *es = static_cast<ExprStmt *>(s);
    checkExpr(es->getExpr());
    break;
  }
  case StmtKind::Break:
  case StmtKind::Continue:
  case StmtKind::Item:
    break;
  }
}

void Sema::checkCompoundStmt(CompoundStmt *s) {
  pushScope();
  for (auto *stmt : s->getStmts())
    checkStmt(stmt);
  if (s->getTrailingExpr())
    checkExpr(s->getTrailingExpr());
  popScope();
}

void Sema::checkReturnStmt(ReturnStmt *s) {
  if (s->getValue()) {
    Type *valType = checkExpr(s->getValue());
    if (currentReturnType && valType && !isCompatible(currentReturnType, valType)) {
      diags.emitError(s->getLocation(), DiagID::ErrTypeMismatch,
                      "return type mismatch");
    }
  }
}

} // namespace asc
