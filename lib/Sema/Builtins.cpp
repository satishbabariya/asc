// Builtins.cpp — pre-registers compiler-intrinsic types and methods in Sema.
// Provides Option, Result, Vec, String, Box, and built-in methods.

#include "asc/Sema/Sema.h"

namespace asc {

/// Register all built-in types and methods in the global scope.
/// Called at the start of Sema::analyze().
void registerBuiltins(ASTContext &ctx, Scope *scope,
                      llvm::StringMap<StructDecl *> &structDecls,
                      llvm::StringMap<EnumDecl *> &enumDecls,
                      llvm::StringMap<TraitDecl *> &traitDecls) {
  SourceLocation loc; // invalid loc for builtins

  // --- Option<T> ---
  // DECISION: Option represented as enum with two variants.
  // At the MLIR level, this becomes { tag: i1, payload: T }.
  {
    auto *someType = ctx.create<NamedType>("T", std::vector<Type *>{}, loc);
    auto *someVariant = ctx.create<EnumVariantDecl>(
        "Some", EnumVariantDecl::VariantKind::Tuple,
        std::vector<Type *>{ctx.create<OwnType>(someType, loc)},
        std::vector<FieldDecl *>{}, nullptr, loc);
    auto *noneVariant = ctx.create<EnumVariantDecl>(
        "None", EnumVariantDecl::VariantKind::Unit, std::vector<Type *>{},
        std::vector<FieldDecl *>{}, nullptr, loc);

    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *optionEnum = ctx.create<EnumDecl>(
        "Option", std::vector<GenericParam>{gp},
        std::vector<EnumVariantDecl *>{someVariant, noneVariant}, loc);
    enumDecls["Option"] = optionEnum;

    Symbol sym;
    sym.name = "Option";
    sym.decl = optionEnum;
    scope->declare("Option", std::move(sym));
  }

  // --- Result<T, E> ---
  {
    auto *okType = ctx.create<NamedType>("T", std::vector<Type *>{}, loc);
    auto *errType = ctx.create<NamedType>("E", std::vector<Type *>{}, loc);
    auto *okVariant = ctx.create<EnumVariantDecl>(
        "Ok", EnumVariantDecl::VariantKind::Tuple,
        std::vector<Type *>{ctx.create<OwnType>(okType, loc)},
        std::vector<FieldDecl *>{}, nullptr, loc);
    auto *errVariant = ctx.create<EnumVariantDecl>(
        "Err", EnumVariantDecl::VariantKind::Tuple,
        std::vector<Type *>{ctx.create<OwnType>(errType, loc)},
        std::vector<FieldDecl *>{}, nullptr, loc);

    GenericParam gpT, gpE;
    gpT.name = "T";
    gpT.loc = loc;
    gpE.name = "E";
    gpE.loc = loc;
    auto *resultEnum = ctx.create<EnumDecl>(
        "Result", std::vector<GenericParam>{gpT, gpE},
        std::vector<EnumVariantDecl *>{okVariant, errVariant}, loc);
    enumDecls["Result"] = resultEnum;

    Symbol sym;
    sym.name = "Result";
    sym.decl = resultEnum;
    scope->declare("Result", std::move(sym));
  }

  // --- Vec<T> ---
  // Struct: { ptr: *mut T, len: usize, cap: usize }
  {
    auto *usizeTy = ctx.getBuiltinType(BuiltinTypeKind::USize);
    auto *ptrField = ctx.create<FieldDecl>(
        "ptr", ctx.getBuiltinType(BuiltinTypeKind::USize), loc);
    auto *lenField = ctx.create<FieldDecl>("len", usizeTy, loc);
    auto *capField = ctx.create<FieldDecl>("cap", usizeTy, loc);

    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *vecStruct = ctx.create<StructDecl>(
        "Vec", std::vector<GenericParam>{gp},
        std::vector<FieldDecl *>{ptrField, lenField, capField}, loc);
    structDecls["Vec"] = vecStruct;

    Symbol sym;
    sym.name = "Vec";
    sym.decl = vecStruct;
    scope->declare("Vec", std::move(sym));
  }

  // --- String ---
  // Alias for Vec<u8> with UTF-8 invariant.
  {
    auto *usizeTy = ctx.getBuiltinType(BuiltinTypeKind::USize);
    auto *ptrField = ctx.create<FieldDecl>(
        "ptr", ctx.getBuiltinType(BuiltinTypeKind::USize), loc);
    auto *lenField = ctx.create<FieldDecl>("len", usizeTy, loc);
    auto *capField = ctx.create<FieldDecl>("cap", usizeTy, loc);

    auto *strStruct = ctx.create<StructDecl>(
        "String", std::vector<GenericParam>{},
        std::vector<FieldDecl *>{ptrField, lenField, capField}, loc);
    structDecls["String"] = strStruct;

    Symbol sym;
    sym.name = "String";
    sym.decl = strStruct;
    scope->declare("String", std::move(sym));
  }

  // --- Box<T> ---
  {
    auto *ptrField = ctx.create<FieldDecl>(
        "ptr", ctx.getBuiltinType(BuiltinTypeKind::USize), loc);
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *boxStruct = ctx.create<StructDecl>(
        "Box", std::vector<GenericParam>{gp},
        std::vector<FieldDecl *>{ptrField}, loc);
    structDecls["Box"] = boxStruct;

    Symbol sym;
    sym.name = "Box";
    sym.decl = boxStruct;
    scope->declare("Box", std::move(sym));
  }
  // --- Arc<T> ---
  // Atomic reference counted shared ownership.
  {
    auto *ptrField = ctx.create<FieldDecl>(
        "ptr", ctx.getBuiltinType(BuiltinTypeKind::USize), loc);
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *arcStruct = ctx.create<StructDecl>(
        "Arc", std::vector<GenericParam>{gp},
        std::vector<FieldDecl *>{ptrField}, loc);
    arcStruct->addAttribute("@send");
    arcStruct->addAttribute("@sync");
    structDecls["Arc"] = arcStruct;

    Symbol sym;
    sym.name = "Arc";
    sym.decl = arcStruct;
    scope->declare("Arc", std::move(sym));
  }

  // --- Core Traits ---

  // Drop trait: fn drop(refmut<Self>): void
  {
    auto *dropTrait = ctx.create<TraitDecl>(
        "Drop", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Drop"] = dropTrait;
    Symbol sym;
    sym.name = "Drop";
    sym.decl = dropTrait;
    scope->declare("Drop", std::move(sym));
  }

  // Clone trait: fn clone(ref<Self>): own<Self>
  {
    auto *cloneTrait = ctx.create<TraitDecl>(
        "Clone", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Clone"] = cloneTrait;
    Symbol sym;
    sym.name = "Clone";
    sym.decl = cloneTrait;
    scope->declare("Clone", std::move(sym));
  }

  // PartialEq trait: fn eq(ref<Self>, ref<Self>): bool
  {
    auto *partialEqTrait = ctx.create<TraitDecl>(
        "PartialEq", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["PartialEq"] = partialEqTrait;
    Symbol sym;
    sym.name = "PartialEq";
    sym.decl = partialEqTrait;
    scope->declare("PartialEq", std::move(sym));
  }

  // Eq trait (marker, extends PartialEq)
  {
    auto *eqTrait = ctx.create<TraitDecl>(
        "Eq", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Eq"] = eqTrait;
    Symbol sym;
    sym.name = "Eq";
    sym.decl = eqTrait;
    scope->declare("Eq", std::move(sym));
  }

  // Iterator trait: fn next(refmut<Self>): Option<T>
  {
    GenericParam gp;
    gp.name = "Item";
    gp.loc = loc;
    auto *iterTrait = ctx.create<TraitDecl>(
        "Iterator", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Iterator"] = iterTrait;
    Symbol sym;
    sym.name = "Iterator";
    sym.decl = iterTrait;
    scope->declare("Iterator", std::move(sym));
  }

  // Display trait: fn fmt(ref<Self>, refmut<Formatter>): void
  {
    auto *displayTrait = ctx.create<TraitDecl>(
        "Display", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Display"] = displayTrait;
    Symbol sym;
    sym.name = "Display";
    sym.decl = displayTrait;
    scope->declare("Display", std::move(sym));
  }

  // Debug trait (same as Display but for debug formatting)
  {
    auto *debugTrait = ctx.create<TraitDecl>(
        "Debug", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Debug"] = debugTrait;
    Symbol sym;
    sym.name = "Debug";
    sym.decl = debugTrait;
    scope->declare("Debug", std::move(sym));
  }

  // Send/Sync marker traits (no methods)
  {
    auto *sendTrait = ctx.create<TraitDecl>(
        "Send", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Send"] = sendTrait;
    auto *syncTrait = ctx.create<TraitDecl>(
        "Sync", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Sync"] = syncTrait;
  }

  // Copy marker trait (no methods)
  {
    auto *copyTrait = ctx.create<TraitDecl>(
        "Copy", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Copy"] = copyTrait;
    Symbol sym;
    sym.name = "Copy";
    sym.decl = copyTrait;
    scope->declare("Copy", std::move(sym));
  }

  // Default trait: fn default(): Self
  {
    auto *defaultTrait = ctx.create<TraitDecl>(
        "Default", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{}, loc);
    traitDecls["Default"] = defaultTrait;
    Symbol sym;
    sym.name = "Default";
    sym.decl = defaultTrait;
    scope->declare("Default", std::move(sym));
  }
}

} // namespace asc
