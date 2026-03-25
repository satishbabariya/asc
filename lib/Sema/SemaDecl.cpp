#include "asc/Sema/Sema.h"

namespace asc {

void Sema::checkFunctionDecl(FunctionDecl *d) {
  // Register function in current scope.
  Symbol sym;
  sym.name = d->getName().str();
  sym.decl = d;
  sym.type = d->getReturnType();
  if (!currentScope->declare(d->getName(), std::move(sym))) {
    diags.emitError(d->getLocation(), DiagID::ErrDuplicateDeclaration,
                    "duplicate function declaration '" + d->getName().str() + "'");
  }

  // Check body.
  if (d->getBody()) {
    pushScope();

    // Register parameters.
    for (const auto &param : d->getParams()) {
      Symbol psym;
      psym.name = param.name;
      psym.type = param.type;
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

  // Validate @copy: all fields must be copy types.
  bool hasCopy = false;
  for (const auto &attr : d->getAttributes()) {
    if (attr == "@copy")
      hasCopy = true;
  }

  if (hasCopy) {
    for (auto *field : d->getFields()) {
      if (field->getType() && !isCopyType(field->getType())) {
        diags.emitError(field->getLocation(), DiagID::ErrMissingCopyAttribute,
                        "field '" + field->getName().str() +
                        "' is not @copy but struct is marked @copy");
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
  for (auto *method : d->getMethods())
    checkFunctionDecl(method);
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

  // Register in scope.
  if (!d->getName().empty()) {
    Symbol sym;
    sym.name = d->getName().str();
    sym.decl = d;
    sym.type = type;
    sym.isMutable = !d->isConst();
    if (!currentScope->declare(d->getName(), std::move(sym))) {
      diags.emitError(d->getLocation(), DiagID::ErrDuplicateDeclaration,
                      "duplicate variable declaration '" +
                      d->getName().str() + "'");
    }
  }
}

} // namespace asc
