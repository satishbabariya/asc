#include "asc/Sema/Sema.h"

namespace asc {

Type *Sema::resolveType(Type *t) {
  if (!t)
    return nullptr;

  // If it's a named type, try to resolve to a struct/enum.
  if (auto *nt = dynamic_cast<NamedType *>(t)) {
    // Check if it's a known struct.
    auto sit = structDecls.find(nt->getName());
    if (sit != structDecls.end())
      return t; // keep as named, resolved

    auto eit = enumDecls.find(nt->getName());
    if (eit != enumDecls.end())
      return t;

    // Check scope for type aliases.
    Symbol *sym = currentScope->lookup(nt->getName());
    if (sym && sym->decl) {
      if (auto *ta = dynamic_cast<TypeAliasDecl *>(sym->decl))
        return resolveType(ta->getAliasedType());
    }
  }

  return t;
}

bool Sema::isCompatible(Type *lhs, Type *rhs) {
  if (!lhs || !rhs)
    return true; // unknown types are compatible (for now)

  if (lhs == rhs)
    return true;

  // Same kind check.
  if (lhs->getKind() != rhs->getKind())
    return false;

  // Builtin type comparison.
  if (auto *lb = dynamic_cast<BuiltinType *>(lhs)) {
    if (auto *rb = dynamic_cast<BuiltinType *>(rhs))
      return lb->getBuiltinKind() == rb->getBuiltinKind();
    return false;
  }

  // Named types compare by name.
  if (auto *ln = dynamic_cast<NamedType *>(lhs)) {
    if (auto *rn = dynamic_cast<NamedType *>(rhs))
      return ln->getName() == rn->getName();
    return false;
  }

  // Own/Ref/RefMut: compare inner types.
  if (auto *lo = dynamic_cast<OwnType *>(lhs)) {
    if (auto *ro = dynamic_cast<OwnType *>(rhs))
      return isCompatible(lo->getInner(), ro->getInner());
    return false;
  }
  if (auto *lr = dynamic_cast<RefType *>(lhs)) {
    if (auto *rr = dynamic_cast<RefType *>(rhs))
      return isCompatible(lr->getInner(), rr->getInner());
    return false;
  }
  if (auto *lrm = dynamic_cast<RefMutType *>(lhs)) {
    if (auto *rrm = dynamic_cast<RefMutType *>(rhs))
      return isCompatible(lrm->getInner(), rrm->getInner());
    return false;
  }

  return false;
}

bool Sema::isCopyType(Type *t) {
  if (!t)
    return false;

  // All builtin primitive types are @copy.
  if (dynamic_cast<BuiltinType *>(t))
    return true;

  // Named types: check if the struct has @copy attribute.
  if (auto *nt = dynamic_cast<NamedType *>(t)) {
    auto it = structDecls.find(nt->getName());
    if (it != structDecls.end()) {
      for (const auto &attr : it->second->getAttributes()) {
        if (attr == "@copy")
          return true;
      }
    }
    return false;
  }

  // Tuple: copy if all elements are copy.
  if (auto *tt = dynamic_cast<TupleType *>(t)) {
    for (auto *elem : tt->getElements()) {
      if (!isCopyType(elem))
        return false;
    }
    return true;
  }

  // Array: copy if element is copy.
  if (auto *at = dynamic_cast<ArrayType *>(t))
    return isCopyType(at->getElementType());

  return false;
}

bool Sema::isSendType(Type *t) {
  if (!t)
    return false;

  // Primitives are Send.
  if (dynamic_cast<BuiltinType *>(t))
    return true;

  // Named types: check @send attribute or all fields Send.
  if (auto *nt = dynamic_cast<NamedType *>(t)) {
    auto it = structDecls.find(nt->getName());
    if (it != structDecls.end()) {
      for (const auto &attr : it->second->getAttributes()) {
        if (attr == "@send")
          return true;
      }
      // Default: check all fields.
      for (auto *field : it->second->getFields()) {
        if (!isSendType(field->getType()))
          return false;
      }
      return true;
    }
    return true; // unknown types assumed Send for now
  }

  // own<T> is Send if T is Send.
  if (auto *ot = dynamic_cast<OwnType *>(t))
    return isSendType(ot->getInner());

  // ref<T> is Send if T is Sync.
  if (auto *rt = dynamic_cast<RefType *>(t))
    return isSyncType(rt->getInner());

  // refmut<T> is never Send.
  if (dynamic_cast<RefMutType *>(t))
    return false;

  return true;
}

bool Sema::isSyncType(Type *t) {
  if (!t)
    return false;

  // Primitives are Sync.
  if (dynamic_cast<BuiltinType *>(t))
    return true;

  if (auto *nt = dynamic_cast<NamedType *>(t)) {
    auto it = structDecls.find(nt->getName());
    if (it != structDecls.end()) {
      for (const auto &attr : it->second->getAttributes()) {
        if (attr == "@sync")
          return true;
      }
    }
    return true;
  }

  return true;
}

void Sema::rejectUnsupportedFeature(llvm::StringRef feature,
                                    SourceLocation loc) {
  diags.emitError(loc, DiagID::ErrUnsupportedFeature,
                  "unsupported TypeScript feature: " + feature.str());
}

} // namespace asc
