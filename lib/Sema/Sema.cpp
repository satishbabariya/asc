#include "asc/Sema/Sema.h"
#include <algorithm>

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
  registerBuiltins(ctx, currentScope, structDecls, enumDecls, traitDecls);
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

/// Synthesize impl blocks for @derive attributes on structs.
static void synthesizeDeriveImpls(ASTContext &ctx,
                                   std::vector<Decl *> &items) {
  std::vector<Decl *> syntheticImpls;

  for (auto *item : items) {
    auto *sd = dynamic_cast<StructDecl *>(item);
    if (!sd) continue;

    SourceLocation loc = sd->getLocation();
    std::string typeName = sd->getName().str();

    bool hasClone = false, hasPartialEq = false;
    for (const auto &attr : sd->getAttributes()) {
      if (attr == "@clone") hasClone = true;
      if (attr == "@partialeq") hasPartialEq = true;
    }

    // derive(Clone): fn clone(ref<Self>): own<Type> { return Type { f1: self.f1, ... }; }
    if (hasClone) {
      std::vector<FieldInit> fieldInits;
      for (auto *field : sd->getFields()) {
        auto *selfRef = ctx.create<DeclRefExpr>("self", loc);
        auto *fieldAccess = ctx.create<FieldAccessExpr>(
            selfRef, field->getName().str(), loc);
        FieldInit fi;
        fi.name = field->getName().str();
        fi.value = fieldAccess;
        fi.loc = loc;
        fieldInits.push_back(fi);
      }
      auto *structLit = ctx.create<StructLiteral>(
          typeName, std::move(fieldInits), nullptr, loc);
      auto *retStmt = ctx.create<ReturnStmt>(structLit, loc);
      auto *body = ctx.create<CompoundStmt>(
          std::vector<Stmt *>{retStmt}, nullptr, loc);

      auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
      auto *selfRefType = ctx.create<RefType>(selfType, loc);
      ParamDecl selfParam;
      selfParam.name = "self";
      selfParam.type = selfRefType;
      selfParam.isSelfRef = true;
      selfParam.loc = loc;
      auto *retType = ctx.create<NamedType>(typeName, std::vector<Type *>{}, loc);

      auto *cloneMethod = ctx.create<FunctionDecl>(
          "clone", std::vector<GenericParam>{},
          std::vector<ParamDecl>{selfParam},
          retType, body, std::vector<WhereConstraint>{}, loc);

      auto *targetType = ctx.create<NamedType>(typeName, std::vector<Type *>{}, loc);
      auto *traitType = ctx.create<NamedType>("Clone", std::vector<Type *>{}, loc);
      auto *implDecl = ctx.create<ImplDecl>(
          std::vector<GenericParam>{}, targetType, traitType,
          std::vector<FunctionDecl *>{cloneMethod}, loc);
      syntheticImpls.push_back(implDecl);
    }

    // derive(PartialEq): fn eq(ref<Self>, other: ref<Type>): bool { return self.f1 == other.f1 && ...; }
    if (hasPartialEq) {
      Expr *comparison = nullptr;
      for (auto *field : sd->getFields()) {
        std::string fname = field->getName().str();
        auto *selfRef = ctx.create<DeclRefExpr>("self", loc);
        auto *selfField = ctx.create<FieldAccessExpr>(selfRef, fname, loc);
        auto *otherRef = ctx.create<DeclRefExpr>("other", loc);
        auto *otherField = ctx.create<FieldAccessExpr>(otherRef, fname, loc);
        auto *fieldEq = ctx.create<BinaryExpr>(BinaryOp::Eq, selfField, otherField, loc);
        if (!comparison) comparison = fieldEq;
        else comparison = ctx.create<BinaryExpr>(BinaryOp::LogAnd, comparison, fieldEq, loc);
      }
      if (!comparison)
        comparison = ctx.create<BoolLiteral>(true, loc);

      auto *retStmt = ctx.create<ReturnStmt>(comparison, loc);
      auto *body = ctx.create<CompoundStmt>(
          std::vector<Stmt *>{retStmt}, nullptr, loc);

      auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
      auto *selfRefType = ctx.create<RefType>(selfType, loc);
      ParamDecl selfParam;
      selfParam.name = "self";
      selfParam.type = selfRefType;
      selfParam.isSelfRef = true;
      selfParam.loc = loc;

      auto *otherNamedType = ctx.create<NamedType>(typeName, std::vector<Type *>{}, loc);
      auto *otherRefType = ctx.create<RefType>(otherNamedType, loc);
      ParamDecl otherParam;
      otherParam.name = "other";
      otherParam.type = otherRefType;
      otherParam.loc = loc;

      auto *boolType = ctx.getBuiltinType(BuiltinTypeKind::Bool);
      auto *eqMethod = ctx.create<FunctionDecl>(
          "eq", std::vector<GenericParam>{},
          std::vector<ParamDecl>{selfParam, otherParam},
          boolType, body, std::vector<WhereConstraint>{}, loc);

      auto *targetType = ctx.create<NamedType>(typeName, std::vector<Type *>{}, loc);
      auto *traitType = ctx.create<NamedType>("PartialEq", std::vector<Type *>{}, loc);
      auto *implDecl = ctx.create<ImplDecl>(
          std::vector<GenericParam>{}, targetType, traitType,
          std::vector<FunctionDecl *>{eqMethod}, loc);
      syntheticImpls.push_back(implDecl);
    }
  }

  for (auto *impl : syntheticImpls)
    items.push_back(impl);
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
    else if (auto *ta = dynamic_cast<TypeAliasDecl *>(item))
      typeAliases[ta->getName()] = ta->getAliasedType();
    else if (auto *id = dynamic_cast<ImplDecl *>(item)) {
      if (auto *nt = dynamic_cast<NamedType *>(id->getTargetType()))
        implDecls[nt->getName()].push_back(id);
    }
    else if (auto *ed = dynamic_cast<ExportDecl *>(item)) {
      if (auto *inner = ed->getInner()) {
        if (auto *sd = dynamic_cast<StructDecl *>(inner))
          structDecls[sd->getName()] = sd;
        else if (auto *enm = dynamic_cast<EnumDecl *>(inner))
          enumDecls[enm->getName()] = enm;
      }
    }
  }

  // Run @derive expansion on each struct first (this happens inside
  // checkStructDecl too, but we need attributes settled before synthesis).
  for (auto *item : items) {
    if (auto *sd = dynamic_cast<StructDecl *>(item))
      checkStructDecl(sd);
  }

  // Synthesize impl blocks for @derive attributes — runs AFTER type
  // registration so synthesized impls can reference struct fields.
  synthesizeDeriveImpls(ctx, items);

  // Register the newly-synthesized impl blocks.
  for (auto *item : items) {
    if (auto *id = dynamic_cast<ImplDecl *>(item)) {
      if (auto *nt = dynamic_cast<NamedType *>(id->getTargetType())) {
        auto &v = implDecls[nt->getName()];
        if (std::find(v.begin(), v.end(), id) == v.end())
          v.push_back(id);
      }
    }
  }

  // Second pass: register all function signatures (enables forward references).
  for (auto *item : items) {
    FunctionDecl *fd = nullptr;
    if (auto *f = dynamic_cast<FunctionDecl *>(item))
      fd = f;
    else if (auto *ed = dynamic_cast<ExportDecl *>(item)) {
      if (ed->getInner())
        fd = dynamic_cast<FunctionDecl *>(ed->getInner());
    }
    if (fd) {
      Symbol sym;
      sym.name = fd->getName().str();
      sym.decl = fd;
      sym.type = fd->getReturnType();
      if (!currentScope->declare(fd->getName(), std::move(sym))) {
        diags.emitError(fd->getLocation(), DiagID::ErrDuplicateDeclaration,
                        "duplicate function declaration '" +
                        fd->getName().str() + "'");
      }
    }
  }

  // Third pass: check all declarations (bodies, types, etc.).
  // Skip structs since we already checked them above.
  for (auto *item : items) {
    if (dynamic_cast<StructDecl *>(item)) continue;
    checkDecl(item);
  }
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
  case DeclKind::Const: {
    auto *cd = static_cast<ConstDecl *>(d);
    if (cd->getInit()) checkExpr(cd->getInit());
    Symbol sym;
    sym.name = cd->getName().str();
    sym.decl = cd;
    sym.type = cd->getType();
    currentScope->declare(cd->getName(), std::move(sym));
    break;
  }
  case DeclKind::Static: {
    auto *sd = static_cast<StaticDecl *>(d);
    if (sd->getInit()) checkExpr(sd->getInit());
    Symbol sym;
    sym.name = sd->getName().str();
    sym.decl = sd;
    sym.type = sd->getType();
    sym.isMutable = sd->isMut();
    currentScope->declare(sd->getName(), std::move(sym));
    break;
  }
  case DeclKind::TypeAlias:
  case DeclKind::Import:
    // DECISION: Import resolution handled in Driver (multi-file pipeline).
    // At Sema level, imported symbols are already merged into scope by Driver.
    break;
  case DeclKind::Export: {
    auto *ed = static_cast<ExportDecl *>(d);
    if (ed->getInner()) checkDecl(ed->getInner());
    break;
  }
  case DeclKind::Field:
  case DeclKind::EnumVariant:
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

  // W004: Check for owned values that are never used — resource leaks.
  currentScope->forEachSymbol([&](llvm::StringRef name, Symbol &sym) {
    if (!sym.decl || sym.ownership.isCopy || sym.isMoved || sym.isUsed)
      return;
    if (!sym.type || isCopyType(sym.type))
      return;
    if (dynamic_cast<FunctionDecl *>(sym.decl))
      return;
    diags.emitWarning(sym.decl->getLocation(), DiagID::WarnResourceLeak,
                      "owned value '" + name.str() +
                      "' is never used — this is a resource leak");
  });

  // W005: Check for unused variables (including copy types).
  // Skip variables whose names start with underscore (conventional ignore).
  currentScope->forEachSymbol([&](llvm::StringRef name, Symbol &sym) {
    if (!sym.decl || sym.isUsed || sym.isMoved)
      return;
    if (name.starts_with("_"))
      return;
    if (dynamic_cast<FunctionDecl *>(sym.decl))
      return;
    // Only warn for variable declarations (let/const).
    if (!dynamic_cast<VarDecl *>(sym.decl))
      return;
    // Skip if already warned via W004 (owned non-copy).
    if (sym.type && !isCopyType(sym.type) && !sym.ownership.isCopy)
      return;
    diags.report(sym.decl->getLocation(), DiagID::WarnUnusedVariable,
                 "unused variable '" + name.str() + "'")
        .addFixIt(SourceRange{sym.decl->getLocation(), sym.decl->getLocation()},
                  "_" + name.str());
  });

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
