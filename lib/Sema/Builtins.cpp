// Builtins.cpp — pre-registers compiler-intrinsic types and methods in Sema.
// Provides Option, Result, Vec, String, Box, and built-in methods.

#include "asc/Sema/Sema.h"

namespace asc {

/// Register all built-in types and methods in the global scope.
/// Called at the start of Sema::analyze().
void registerBuiltins(ASTContext &ctx, Scope *scope,
                      llvm::StringMap<StructDecl *> &structDecls,
                      llvm::StringMap<EnumDecl *> &enumDecls) {
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
}

} // namespace asc
