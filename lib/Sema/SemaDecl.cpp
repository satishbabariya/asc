#include "asc/Sema/Sema.h"

namespace asc {

void Sema::checkFunctionDecl(FunctionDecl *d) {
  // Register function in current scope (skip if already pre-registered for
  // forward reference support — the analyze() pre-pass registers names).
  if (!currentScope->lookupLocal(d->getName())) {
    Symbol sym;
    sym.name = d->getName().str();
    sym.decl = d;
    sym.type = d->getReturnType();
    currentScope->declare(d->getName(), std::move(sym));
  }

  // Check body.
  if (d->getBody()) {
    pushScope();

    // Register parameters with ownership from type annotations.
    for (const auto &param : d->getParams()) {
      Symbol psym;
      psym.name = param.name;
      psym.type = resolveType(param.type);
      psym.ownership.kind = inferParamOwnership(param.type);
      psym.ownership.isCopy = param.type ? isCopyType(param.type) : false;
      psym.ownership.isSend = param.type ? isSendType(param.type) : false;
      psym.ownership.isSync = param.type ? isSyncType(param.type) : false;
      currentScope->declare(param.name, std::move(psym));
    }

    Type *prevReturn = currentReturnType;
    currentReturnType = d->getReturnType();

    checkCompoundStmt(d->getBody());

    currentReturnType = prevReturn;
    popScope();
  }
}

void Sema::checkStructDecl(StructDecl *d) {
  // Register struct in scope.
  Symbol sym;
  sym.name = d->getName().str();
  sym.decl = d;
  currentScope->declare(d->getName(), std::move(sym));

  // Parse attributes.
  bool hasCopy = false, hasSend = false, hasSync = false;
  for (const auto &attr : d->getAttributes()) {
    if (attr == "@copy") hasCopy = true;
    if (attr == "@send") hasSend = true;
    if (attr == "@sync") hasSync = true;
  }

  // Validate @copy: all fields must be copy types.
  if (hasCopy) {
    for (auto *field : d->getFields()) {
      if (field->getType() && !isCopyType(field->getType())) {
        diags.emitError(field->getLocation(), DiagID::ErrMissingCopyAttribute,
                        "field '" + field->getName().str() +
                        "' is not @copy but struct is marked @copy");
      }
    }
  }

  // Validate @send: all fields must be Send.
  if (hasSend) {
    for (auto *field : d->getFields()) {
      if (field->getType() && !isSendType(field->getType())) {
        diags.emitError(field->getLocation(), DiagID::ErrNonSendCaptured,
                        "field '" + field->getName().str() +
                        "' is not Send but struct is marked @send");
      }
    }
  }

  // Validate @sync: all fields must be Sync.
  if (hasSync) {
    for (auto *field : d->getFields()) {
      if (field->getType() && !isSyncType(field->getType())) {
        diags.emitError(field->getLocation(), DiagID::ErrNonSendCaptured,
                        "field '" + field->getName().str() +
                        "' is not Sync but struct is marked @sync");
      }
    }
  }
}

void Sema::checkEnumDecl(EnumDecl *d) {
  Symbol sym;
  sym.name = d->getName().str();
  sym.decl = d;
  currentScope->declare(d->getName(), std::move(sym));
}

void Sema::checkTraitDecl(TraitDecl *d) {
  Symbol sym;
  sym.name = d->getName().str();
  sym.decl = d;
  currentScope->declare(d->getName(), std::move(sym));

  // Check all methods.
  for (const auto &item : d->getItems()) {
    if (item.method)
      checkFunctionDecl(item.method);
  }
}

void Sema::checkImplDecl(ImplDecl *d) {
  // Register impl by target type name for method resolution.
  if (auto *nt = dynamic_cast<NamedType *>(d->getTargetType())) {
    implDecls[nt->getName()].push_back(d);
  }

  // If this is a trait impl, verify all required methods are provided.
  if (d->isTraitImpl()) {
    if (auto *namedType = dynamic_cast<NamedType *>(d->getTraitType())) {
      auto it = traitDecls.find(namedType->getName());
      if (it != traitDecls.end()) {
        TraitDecl *trait = it->second;
        for (const auto &item : trait->getItems()) {
          if (!item.method || item.method->getBody())
            continue; // has default impl or is assoc type
          // Check that impl provides this method.
          bool found = false;
          for (auto *m : d->getMethods()) {
            if (m->getName() == item.method->getName()) {
              found = true;
              break;
            }
          }
          if (!found) {
            diags.emitError(
                d->getLocation(), DiagID::ErrTraitNotImplemented,
                "missing implementation of '" +
                item.method->getName().str() + "' from trait '" +
                namedType->getName().str() + "'");
          }
        }
      }
    }
  }

  pushScope();
  for (auto *method : d->getMethods()) {
    // Set the self parameter type to the impl's target type.
    for (auto &param : method->getMutableParams()) {
      if ((param.isSelfRef || param.isSelfRefMut || param.isSelfOwn) &&
          !param.type) {
        if (param.isSelfRef)
          param.type = ctx.create<RefType>(d->getTargetType(),
                                            method->getLocation());
        else if (param.isSelfRefMut)
          param.type = ctx.create<RefMutType>(d->getTargetType(),
                                               method->getLocation());
        else
          param.type = d->getTargetType();
      }
    }
    checkFunctionDecl(method);
  }
  popScope();
}

void Sema::checkVarDecl(VarDecl *d) {
  Type *type = d->getType();

  // Infer type from initializer.
  if (d->getInit()) {
    Type *initType = checkExpr(d->getInit());
    if (!type)
      type = initType;
    else if (initType && !isCompatible(type, initType)) {
      diags.emitError(d->getLocation(), DiagID::ErrTypeMismatch,
                      "type mismatch in variable declaration");
    }
    d->setType(type);
  }

  // Determine ownership.
  OwnershipInfo ownerInfo;
  if (type) {
    if (dynamic_cast<OwnType *>(type))
      ownerInfo.kind = OwnershipKind::Owned;
    else if (dynamic_cast<RefType *>(type))
      ownerInfo.kind = OwnershipKind::Borrowed;
    else if (dynamic_cast<RefMutType *>(type))
      ownerInfo.kind = OwnershipKind::BorrowedMut;
    else if (isCopyType(type))
      ownerInfo.kind = OwnershipKind::Copied;
    else
      ownerInfo.kind = OwnershipKind::Owned; // Default: owned
    ownerInfo.isCopy = isCopyType(type);
    ownerInfo.isSend = isSendType(type);
    ownerInfo.isSync = isSyncType(type);
  }
  varOwnership[d] = ownerInfo;

  // Register in scope.
  if (!d->getName().empty()) {
    Symbol sym;
    sym.name = d->getName().str();
    sym.decl = d;
    sym.type = type;
    sym.isMutable = !d->isConst();
    sym.ownership = ownerInfo;
    if (!currentScope->declare(d->getName(), std::move(sym))) {
      diags.emitError(d->getLocation(), DiagID::ErrDuplicateDeclaration,
                      "duplicate variable declaration '" +
                      d->getName().str() + "'");
    }
  }

  // Handle destructuring patterns: const [a, b] = expr
  if (d->getName().empty() && d->getPattern()) {
    auto declarePatternBindings = [&](Pattern *pat) {
      if (auto *ip = dynamic_cast<IdentPattern *>(pat)) {
        Symbol sym;
        sym.name = ip->getName().str();
        sym.decl = d;
        sym.type = type;
        sym.isMutable = !d->isConst();
        sym.ownership = ownerInfo;
        currentScope->declare(ip->getName(), std::move(sym));
      }
    };
    if (auto *sp = dynamic_cast<SlicePattern *>(d->getPattern())) {
      for (auto *elem : sp->getElements())
        declarePatternBindings(elem);
    } else if (auto *tp = dynamic_cast<TuplePattern *>(d->getPattern())) {
      for (auto *elem : tp->getElements())
        declarePatternBindings(elem);
    } else {
      declarePatternBindings(d->getPattern());
    }
  }
}

} // namespace asc
