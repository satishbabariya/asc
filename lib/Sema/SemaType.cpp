#include "asc/Sema/Sema.h"

namespace asc {

Type *Sema::resolveType(Type *t) {
  if (!t)
    return nullptr;

  if (auto *nt = dynamic_cast<NamedType *>(t)) {
    // If the type has generic args (e.g. Vec<i32>), monomorphize.
    if (!nt->getGenericArgs().empty()) {
      Type *mono = monomorphizeType(nt->getName(), nt->getGenericArgs());
      if (mono)
        return mono;
    }

    // Check if it's a known struct.
    auto sit = structDecls.find(nt->getName());
    if (sit != structDecls.end())
      return t;

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

// --- Generic monomorphization ---

std::string Sema::mangleTypeName(Type *t) {
  if (!t) return "void";
  if (auto *bt = dynamic_cast<BuiltinType *>(t)) {
    switch (bt->getBuiltinKind()) {
    case BuiltinTypeKind::I8: return "i8";
    case BuiltinTypeKind::I16: return "i16";
    case BuiltinTypeKind::I32: return "i32";
    case BuiltinTypeKind::I64: return "i64";
    case BuiltinTypeKind::I128: return "i128";
    case BuiltinTypeKind::U8: return "u8";
    case BuiltinTypeKind::U16: return "u16";
    case BuiltinTypeKind::U32: return "u32";
    case BuiltinTypeKind::U64: return "u64";
    case BuiltinTypeKind::U128: return "u128";
    case BuiltinTypeKind::F32: return "f32";
    case BuiltinTypeKind::F64: return "f64";
    case BuiltinTypeKind::Bool: return "bool";
    case BuiltinTypeKind::Char: return "char";
    case BuiltinTypeKind::USize: return "usize";
    case BuiltinTypeKind::ISize: return "isize";
    case BuiltinTypeKind::Void: return "void";
    case BuiltinTypeKind::Never: return "never";
    }
  }
  if (auto *nt = dynamic_cast<NamedType *>(t))
    return nt->getName().str();
  if (auto *ot = dynamic_cast<OwnType *>(t))
    return "own_" + mangleTypeName(ot->getInner());
  if (auto *rt = dynamic_cast<RefType *>(t))
    return "ref_" + mangleTypeName(rt->getInner());
  return "unknown";
}

std::string Sema::mangleGenericName(llvm::StringRef base,
                                    const std::vector<Type *> &args) {
  std::string mangled = base.str();
  for (auto *a : args)
    mangled += "_" + mangleTypeName(a);
  return mangled;
}

Type *Sema::monomorphizeType(llvm::StringRef baseName,
                             const std::vector<Type *> &typeArgs) {
  std::string mangled = mangleGenericName(baseName, typeArgs);

  // Check cache.
  auto cacheIt = monoCache.find(mangled);
  if (cacheIt != monoCache.end()) {
    return ctx.create<NamedType>(mangled, std::vector<Type *>{},
                                 SourceLocation());
  }

  // Try monomorphizing a struct.
  auto sit = structDecls.find(baseName);
  if (sit != structDecls.end()) {
    StructDecl *generic = sit->second;
    if (generic->getGenericParams().empty()) return nullptr;

    // Build type substitution map: generic param name → concrete type.
    llvm::StringMap<Type *> subst;
    for (unsigned i = 0; i < generic->getGenericParams().size() &&
                         i < typeArgs.size(); ++i) {
      subst[generic->getGenericParams()[i].name] = typeArgs[i];
    }

    // Clone fields with substitution.
    std::vector<FieldDecl *> newFields;
    for (auto *field : generic->getFields()) {
      Type *ft = field->getType();
      // Substitute if field type is a generic parameter.
      if (auto *nt = dynamic_cast<NamedType *>(ft)) {
        auto substIt = subst.find(nt->getName());
        if (substIt != subst.end())
          ft = substIt->second;
      }
      newFields.push_back(ctx.create<FieldDecl>(
          field->getName().str(), ft, field->getLocation()));
    }

    auto *monoStruct = ctx.create<StructDecl>(
        mangled, std::vector<GenericParam>{}, std::move(newFields),
        generic->getLocation());
    // Copy attributes.
    for (const auto &attr : generic->getAttributes())
      monoStruct->addAttribute(attr);

    structDecls[mangled] = monoStruct;
    monoCache[mangled] = monoStruct;
    return ctx.create<NamedType>(mangled, std::vector<Type *>{},
                                 SourceLocation());
  }

  // Try monomorphizing an enum.
  auto eit = enumDecls.find(baseName);
  if (eit != enumDecls.end()) {
    EnumDecl *generic = eit->second;
    if (generic->getGenericParams().empty()) return nullptr;

    llvm::StringMap<Type *> subst;
    for (unsigned i = 0; i < generic->getGenericParams().size() &&
                         i < typeArgs.size(); ++i) {
      subst[generic->getGenericParams()[i].name] = typeArgs[i];
    }

    // Clone variants with substitution.
    std::vector<EnumVariantDecl *> newVariants;
    for (auto *v : generic->getVariants()) {
      std::vector<Type *> newTupleTypes;
      for (auto *tt : v->getTupleTypes()) {
        Type *resolved = tt;
        // Substitute through own<T>, ref<T>, etc.
        if (auto *ot = dynamic_cast<OwnType *>(tt)) {
          if (auto *inner = dynamic_cast<NamedType *>(ot->getInner())) {
            auto substIt = subst.find(inner->getName());
            if (substIt != subst.end())
              resolved = ctx.create<OwnType>(substIt->second, tt->getLocation());
          }
        } else if (auto *nt = dynamic_cast<NamedType *>(tt)) {
          auto substIt = subst.find(nt->getName());
          if (substIt != subst.end())
            resolved = substIt->second;
        }
        newTupleTypes.push_back(resolved);
      }
      newVariants.push_back(ctx.create<EnumVariantDecl>(
          v->getName().str(), v->getVariantKind(), std::move(newTupleTypes),
          std::vector<FieldDecl *>{}, v->getValue(), v->getLocation()));
    }

    auto *monoEnum = ctx.create<EnumDecl>(
        mangled, std::vector<GenericParam>{}, std::move(newVariants),
        generic->getLocation());
    enumDecls[mangled] = monoEnum;
    monoCache[mangled] = monoEnum;
    return ctx.create<NamedType>(mangled, std::vector<Type *>{},
                                 SourceLocation());
  }

  return nullptr;
}

} // namespace asc
