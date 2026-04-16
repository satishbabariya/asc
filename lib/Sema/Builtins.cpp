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

  // --- Rc<T> ---
  // Non-atomic reference counted (single-threaded).
  {
    auto *ptrField = ctx.create<FieldDecl>(
        "ptr", ctx.getBuiltinType(BuiltinTypeKind::USize), loc);
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *rcStruct = ctx.create<StructDecl>(
        "Rc", std::vector<GenericParam>{gp},
        std::vector<FieldDecl *>{ptrField}, loc);
    structDecls["Rc"] = rcStruct;

    Symbol sym;
    sym.name = "Rc";
    sym.decl = rcStruct;
    scope->declare("Rc", std::move(sym));
  }

  // --- Weak<T> ---
  // Weak reference to Rc<T>.
  {
    auto *ptrField = ctx.create<FieldDecl>(
        "ptr", ctx.getBuiltinType(BuiltinTypeKind::USize), loc);
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *weakStruct = ctx.create<StructDecl>(
        "Weak", std::vector<GenericParam>{gp},
        std::vector<FieldDecl *>{ptrField}, loc);
    structDecls["Weak"] = weakStruct;

    Symbol sym;
    sym.name = "Weak";
    sym.decl = weakStruct;
    scope->declare("Weak", std::move(sym));
  }

  // --- Mutex ---
  // Opaque handle: { state: u32 }
  {
    auto *stateField = ctx.create<FieldDecl>(
        "state", ctx.getBuiltinType(BuiltinTypeKind::U32), loc);
    auto *mutexStruct = ctx.create<StructDecl>(
        "Mutex", std::vector<GenericParam>{},
        std::vector<FieldDecl *>{stateField}, loc);
    structDecls["Mutex"] = mutexStruct;

    Symbol sym;
    sym.name = "Mutex";
    sym.decl = mutexStruct;
    scope->declare("Mutex", std::move(sym));
  }

  // --- RwLock ---
  // Opaque handle: { readers: i32, writer: i32 }
  {
    auto *readersField = ctx.create<FieldDecl>(
        "readers", ctx.getBuiltinType(BuiltinTypeKind::I32), loc);
    auto *writerField = ctx.create<FieldDecl>(
        "writer", ctx.getBuiltinType(BuiltinTypeKind::I32), loc);
    auto *rwlockStruct = ctx.create<StructDecl>(
        "RwLock", std::vector<GenericParam>{},
        std::vector<FieldDecl *>{readersField, writerField}, loc);
    structDecls["RwLock"] = rwlockStruct;

    Symbol sym;
    sym.name = "RwLock";
    sym.decl = rwlockStruct;
    scope->declare("RwLock", std::move(sym));
  }

  // --- File ---
  // File handle for I/O operations.
  {
    auto *fileStruct = ctx.create<StructDecl>(
        "File", std::vector<GenericParam>{},
        std::vector<FieldDecl *>{
            ctx.create<FieldDecl>("fd", ctx.getBuiltinType(BuiltinTypeKind::I32), loc)},
        loc);
    structDecls["File"] = fileStruct;
    Symbol sym;
    sym.name = "File";
    sym.decl = fileStruct;
    scope->declare("File", std::move(sym));
  }

  // --- Core Traits ---

  // Drop trait: fn drop(refmut<Self>): void
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRefMut = ctx.create<RefMutType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRefMut;
    selfParam.isSelfRefMut = true;
    selfParam.loc = loc;
    auto *dropMethod = ctx.create<FunctionDecl>(
        "drop", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam},
        ctx.getVoidType(), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem dropItem;
    dropItem.method = dropMethod;
    auto *dropTrait = ctx.create<TraitDecl>(
        "Drop", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{dropItem}, loc);
    traitDecls["Drop"] = dropTrait;
    Symbol sym;
    sym.name = "Drop";
    sym.decl = dropTrait;
    scope->declare("Drop", std::move(sym));
  }

  // Clone trait: fn clone(ref<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    auto *cloneMethod = ctx.create<FunctionDecl>(
        "clone", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem cloneItem;
    cloneItem.method = cloneMethod;
    auto *cloneTrait = ctx.create<TraitDecl>(
        "Clone", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{cloneItem}, loc);
    traitDecls["Clone"] = cloneTrait;
    Symbol sym;
    sym.name = "Clone";
    sym.decl = cloneTrait;
    scope->declare("Clone", std::move(sym));
  }

  // PartialEq trait: fn eq(ref<Self>, ref<Self>): bool
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl otherParam;
    otherParam.name = "other";
    otherParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    otherParam.loc = loc;
    auto *eqMethod = ctx.create<FunctionDecl>(
        "eq", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, otherParam},
        ctx.getBuiltinType(BuiltinTypeKind::Bool), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem eqItem;
    eqItem.method = eqMethod;
    auto *partialEqTrait = ctx.create<TraitDecl>(
        "PartialEq", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{eqItem}, loc);
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

  // Iterator trait: fn next(refmut<Self>): Option<Item>
  {
    GenericParam gp;
    gp.name = "Item";
    gp.loc = loc;
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRefMut = ctx.create<RefMutType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRefMut;
    selfParam.isSelfRefMut = true;
    selfParam.loc = loc;
    auto *itemType = ctx.create<NamedType>("Item", std::vector<Type *>{}, loc);
    auto *optionRetType = ctx.create<NamedType>(
        "Option", std::vector<Type *>{itemType}, loc);
    auto *nextMethod = ctx.create<FunctionDecl>(
        "next", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam},
        optionRetType, nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem nextItem;
    nextItem.method = nextMethod;
    // Associated type Item
    TraitItem itemAssoc;
    itemAssoc.assocTypeName = "Item";
    itemAssoc.isAssocType = true;
    auto *iterTrait = ctx.create<TraitDecl>(
        "Iterator", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{itemAssoc, nextItem}, loc);
    traitDecls["Iterator"] = iterTrait;
    Symbol sym;
    sym.name = "Iterator";
    sym.decl = iterTrait;
    scope->declare("Iterator", std::move(sym));
  }

  // Display trait: fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl fmtParam;
    fmtParam.name = "f";
    fmtParam.type = ctx.create<RefMutType>(
        ctx.create<NamedType>("Formatter", std::vector<Type *>{}, loc), loc);
    fmtParam.loc = loc;
    auto *fmtMethod = ctx.create<FunctionDecl>(
        "fmt", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, fmtParam},
        ctx.create<NamedType>("Result", std::vector<Type *>{
            ctx.getVoidType(),
            ctx.create<NamedType>("FmtError", std::vector<Type *>{}, loc)
        }, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem fmtItem;
    fmtItem.method = fmtMethod;
    auto *displayTrait = ctx.create<TraitDecl>(
        "Display", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{fmtItem}, loc);
    traitDecls["Display"] = displayTrait;
    Symbol sym;
    sym.name = "Display";
    sym.decl = displayTrait;
    scope->declare("Display", std::move(sym));
  }

  // Debug trait: fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl fmtParam;
    fmtParam.name = "f";
    fmtParam.type = ctx.create<RefMutType>(
        ctx.create<NamedType>("Formatter", std::vector<Type *>{}, loc), loc);
    fmtParam.loc = loc;
    auto *fmtMethod = ctx.create<FunctionDecl>(
        "fmt", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, fmtParam},
        ctx.create<NamedType>("Result", std::vector<Type *>{
            ctx.getVoidType(),
            ctx.create<NamedType>("FmtError", std::vector<Type *>{}, loc)
        }, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem fmtItem;
    fmtItem.method = fmtMethod;
    auto *debugTrait = ctx.create<TraitDecl>(
        "Debug", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{fmtItem}, loc);
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
    auto *defaultMethod = ctx.create<FunctionDecl>(
        "default", std::vector<GenericParam>{},
        std::vector<ParamDecl>{},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem defaultItem;
    defaultItem.method = defaultMethod;
    auto *defaultTrait = ctx.create<TraitDecl>(
        "Default", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{defaultItem}, loc);
    traitDecls["Default"] = defaultTrait;
    Symbol sym;
    sym.name = "Default";
    sym.decl = defaultTrait;
    scope->declare("Default", std::move(sym));
  }

  // --- Operator Traits ---

  // Add trait: fn add(ref<Self>, ref<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *addMethod = ctx.create<FunctionDecl>(
        "add", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem addItem;
    addItem.method = addMethod;
    auto *addTrait = ctx.create<TraitDecl>(
        "Add", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{addItem}, loc);
    traitDecls["Add"] = addTrait;
    Symbol sym;
    sym.name = "Add";
    sym.decl = addTrait;
    scope->declare("Add", std::move(sym));
  }

  // Sub trait: fn sub(ref<Self>, ref<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *subMethod = ctx.create<FunctionDecl>(
        "sub", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem subItem;
    subItem.method = subMethod;
    auto *subTrait = ctx.create<TraitDecl>(
        "Sub", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{subItem}, loc);
    traitDecls["Sub"] = subTrait;
    Symbol sym;
    sym.name = "Sub";
    sym.decl = subTrait;
    scope->declare("Sub", std::move(sym));
  }

  // Mul trait: fn mul(ref<Self>, ref<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *mulMethod = ctx.create<FunctionDecl>(
        "mul", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem mulItem;
    mulItem.method = mulMethod;
    auto *mulTrait = ctx.create<TraitDecl>(
        "Mul", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{mulItem}, loc);
    traitDecls["Mul"] = mulTrait;
    Symbol sym;
    sym.name = "Mul";
    sym.decl = mulTrait;
    scope->declare("Mul", std::move(sym));
  }

  // Div trait: fn div(ref<Self>, ref<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *divMethod = ctx.create<FunctionDecl>(
        "div", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem divItem;
    divItem.method = divMethod;
    auto *divTrait = ctx.create<TraitDecl>(
        "Div", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{divItem}, loc);
    traitDecls["Div"] = divTrait;
    Symbol sym;
    sym.name = "Div";
    sym.decl = divTrait;
    scope->declare("Div", std::move(sym));
  }

  // Neg trait: fn neg(ref<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    auto *negMethod = ctx.create<FunctionDecl>(
        "neg", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem negItem;
    negItem.method = negMethod;
    auto *negTrait = ctx.create<TraitDecl>(
        "Neg", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{negItem}, loc);
    traitDecls["Neg"] = negTrait;
    Symbol sym;
    sym.name = "Neg";
    sym.decl = negTrait;
    scope->declare("Neg", std::move(sym));
  }

  // Index trait: fn index(ref<Self>, usize): ref<Self::Output>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl idxParam;
    idxParam.name = "index";
    idxParam.type = ctx.getBuiltinType(BuiltinTypeKind::USize);
    idxParam.loc = loc;
    auto *outputType = ctx.create<NamedType>("Output", std::vector<Type *>{}, loc);
    auto *refOutputType = ctx.create<RefType>(outputType, loc);
    auto *indexMethod = ctx.create<FunctionDecl>(
        "index", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, idxParam},
        refOutputType, nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem indexItem;
    indexItem.method = indexMethod;
    // Associated type Output
    TraitItem outputAssoc;
    outputAssoc.assocTypeName = "Output";
    outputAssoc.isAssocType = true;
    GenericParam gp;
    gp.name = "Output";
    gp.loc = loc;
    auto *indexTrait = ctx.create<TraitDecl>(
        "Index", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{outputAssoc, indexItem}, loc);
    traitDecls["Index"] = indexTrait;
    Symbol sym;
    sym.name = "Index";
    sym.decl = indexTrait;
    scope->declare("Index", std::move(sym));
  }

  // PartialOrd trait: fn partial_cmp(ref<Self>, ref<Self>): Option<Ordering>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl otherParam;
    otherParam.name = "other";
    otherParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    otherParam.loc = loc;
    auto *partialCmpMethod = ctx.create<FunctionDecl>(
        "partial_cmp", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, otherParam},
        ctx.create<NamedType>("Option", std::vector<Type *>{
            ctx.create<NamedType>("Ordering", std::vector<Type *>{}, loc)
        }, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem partialCmpItem;
    partialCmpItem.method = partialCmpMethod;
    auto *partialOrdTrait = ctx.create<TraitDecl>(
        "PartialOrd", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{partialCmpItem}, loc);
    traitDecls["PartialOrd"] = partialOrdTrait;
    Symbol sym;
    sym.name = "PartialOrd";
    sym.decl = partialOrdTrait;
    scope->declare("PartialOrd", std::move(sym));
  }

  // Ord trait: fn cmp(ref<Self>, ref<Self>): Ordering
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl otherParam;
    otherParam.name = "other";
    otherParam.type = ctx.create<RefType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    otherParam.loc = loc;
    auto *cmpMethod = ctx.create<FunctionDecl>(
        "cmp", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, otherParam},
        ctx.create<NamedType>("Ordering", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem cmpItem;
    cmpItem.method = cmpMethod;
    auto *ordTrait = ctx.create<TraitDecl>(
        "Ord", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{cmpItem}, loc);
    traitDecls["Ord"] = ordTrait;
    Symbol sym;
    sym.name = "Ord";
    sym.decl = ordTrait;
    scope->declare("Ord", std::move(sym));
  }

  // Hash trait: fn hash(ref<Self>, refmut<Hasher>): void
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    ParamDecl hasherParam;
    hasherParam.name = "hasher";
    hasherParam.type = ctx.create<RefMutType>(
        ctx.create<NamedType>("Hasher", std::vector<Type *>{}, loc), loc);
    hasherParam.loc = loc;
    auto *hashMethod = ctx.create<FunctionDecl>(
        "hash", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, hasherParam},
        ctx.getVoidType(), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem hashItem;
    hashItem.method = hashMethod;
    auto *hashTrait = ctx.create<TraitDecl>(
        "Hash", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{hashItem}, loc);
    traitDecls["Hash"] = hashTrait;
    Symbol sym;
    sym.name = "Hash";
    sym.decl = hashTrait;
    scope->declare("Hash", std::move(sym));
  }

  // From<T> trait: fn from(T): Self
  {
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    ParamDecl valueParam;
    valueParam.name = "value";
    valueParam.type = ctx.create<NamedType>("T", std::vector<Type *>{}, loc);
    valueParam.loc = loc;
    auto *fromMethod = ctx.create<FunctionDecl>(
        "from", std::vector<GenericParam>{gp},
        std::vector<ParamDecl>{valueParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem fromItem;
    fromItem.method = fromMethod;
    auto *fromTrait = ctx.create<TraitDecl>(
        "From", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{fromItem}, loc);
    traitDecls["From"] = fromTrait;
    Symbol sym;
    sym.name = "From";
    sym.decl = fromTrait;
    scope->declare("From", std::move(sym));
  }

  // Into<T> trait: fn into(self): T
  {
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    auto *intoMethod = ctx.create<FunctionDecl>(
        "into", std::vector<GenericParam>{gp},
        std::vector<ParamDecl>{selfParam},
        ctx.create<NamedType>("T", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem intoItem;
    intoItem.method = intoMethod;
    auto *intoTrait = ctx.create<TraitDecl>(
        "Into", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{intoItem}, loc);
    traitDecls["Into"] = intoTrait;
    Symbol sym;
    sym.name = "Into";
    sym.decl = intoTrait;
    scope->declare("Into", std::move(sym));
  }

  // AsRef<T> trait: fn as_ref(ref<Self>): ref<T>
  {
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    auto *asRefMethod = ctx.create<FunctionDecl>(
        "as_ref", std::vector<GenericParam>{gp},
        std::vector<ParamDecl>{selfParam},
        ctx.create<RefType>(
            ctx.create<NamedType>("T", std::vector<Type *>{}, loc), loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem asRefItem;
    asRefItem.method = asRefMethod;
    auto *asRefTrait = ctx.create<TraitDecl>(
        "AsRef", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{asRefItem}, loc);
    traitDecls["AsRef"] = asRefTrait;
    Symbol sym;
    sym.name = "AsRef";
    sym.decl = asRefTrait;
    scope->declare("AsRef", std::move(sym));
  }

  // AsMut<T> trait: fn as_mut(refmut<Self>): refmut<T>
  {
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRefMut = ctx.create<RefMutType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRefMut;
    selfParam.isSelfRefMut = true;
    selfParam.loc = loc;
    auto *asMutMethod = ctx.create<FunctionDecl>(
        "as_mut", std::vector<GenericParam>{gp},
        std::vector<ParamDecl>{selfParam},
        ctx.create<RefMutType>(
            ctx.create<NamedType>("T", std::vector<Type *>{}, loc), loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem asMutItem;
    asMutItem.method = asMutMethod;
    auto *asMutTrait = ctx.create<TraitDecl>(
        "AsMut", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{asMutItem}, loc);
    traitDecls["AsMut"] = asMutTrait;
    Symbol sym;
    sym.name = "AsMut";
    sym.decl = asMutTrait;
    scope->declare("AsMut", std::move(sym));
  }

  // Deref trait: fn deref(ref<Self>): ref<Target>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRef = ctx.create<RefType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRef;
    selfParam.isSelfRef = true;
    selfParam.loc = loc;
    auto *derefMethod = ctx.create<FunctionDecl>(
        "deref", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam},
        ctx.create<RefType>(
            ctx.create<NamedType>("Target", std::vector<Type *>{}, loc), loc),
        nullptr, std::vector<WhereConstraint>{}, loc);
    TraitItem derefItem;
    derefItem.method = derefMethod;
    auto *derefTrait = ctx.create<TraitDecl>(
        "Deref", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{derefItem}, loc);
    traitDecls["Deref"] = derefTrait;
    Symbol sym;
    sym.name = "Deref";
    sym.decl = derefTrait;
    scope->declare("Deref", std::move(sym));
  }

  // DerefMut trait: fn deref_mut(refmut<Self>): refmut<Target>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRefMut = ctx.create<RefMutType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRefMut;
    selfParam.isSelfRefMut = true;
    selfParam.loc = loc;
    auto *derefMutMethod = ctx.create<FunctionDecl>(
        "deref_mut", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam},
        ctx.create<RefMutType>(
            ctx.create<NamedType>("Target", std::vector<Type *>{}, loc), loc),
        nullptr, std::vector<WhereConstraint>{}, loc);
    TraitItem derefMutItem;
    derefMutItem.method = derefMutMethod;
    auto *derefMutTrait = ctx.create<TraitDecl>(
        "DerefMut", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{derefMutItem}, loc);
    traitDecls["DerefMut"] = derefMutTrait;
    Symbol sym;
    sym.name = "DerefMut";
    sym.decl = derefMutTrait;
    scope->declare("DerefMut", std::move(sym));
  }

  // IntoIterator trait: fn into_iter(own<Self>): own<IntoIter>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfType;
    selfParam.loc = loc;
    auto *intoIterMethod = ctx.create<FunctionDecl>(
        "into_iter", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam},
        ctx.create<NamedType>("IntoIter", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem intoIterItem;
    intoIterItem.method = intoIterMethod;
    TraitItem itemAssoc;
    itemAssoc.assocTypeName = "Item";
    itemAssoc.isAssocType = true;
    TraitItem iterAssoc;
    iterAssoc.assocTypeName = "IntoIter";
    iterAssoc.isAssocType = true;
    auto *intoIterTrait = ctx.create<TraitDecl>(
        "IntoIterator", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{itemAssoc, iterAssoc, intoIterItem}, loc);
    traitDecls["IntoIterator"] = intoIterTrait;
    Symbol sym;
    sym.name = "IntoIterator";
    sym.decl = intoIterTrait;
    scope->declare("IntoIterator", std::move(sym));
  }

  // FromIterator<T> trait: fn from_iter(iter): own<Self>
  {
    GenericParam gp;
    gp.name = "T";
    gp.loc = loc;
    GenericParam gpI;
    gpI.name = "I";
    gpI.loc = loc;
    ParamDecl iterParam;
    iterParam.name = "iter";
    iterParam.type = ctx.create<NamedType>("I", std::vector<Type *>{}, loc);
    iterParam.loc = loc;
    auto *fromIterMethod = ctx.create<FunctionDecl>(
        "from_iter", std::vector<GenericParam>{gpI},
        std::vector<ParamDecl>{iterParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem fromIterItem;
    fromIterItem.method = fromIterMethod;
    auto *fromIterTrait = ctx.create<TraitDecl>(
        "FromIterator", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{fromIterItem}, loc);
    traitDecls["FromIterator"] = fromIterTrait;
    Symbol sym;
    sym.name = "FromIterator";
    sym.decl = fromIterTrait;
    scope->declare("FromIterator", std::move(sym));
  }

  // IndexMut<Idx> trait: fn index_mut(refmut<Self>, Idx): refmut<Output>
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRefMut = ctx.create<RefMutType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRefMut;
    selfParam.isSelfRefMut = true;
    selfParam.loc = loc;
    ParamDecl idxParam;
    idxParam.name = "index";
    idxParam.type = ctx.getBuiltinType(BuiltinTypeKind::USize);
    idxParam.loc = loc;
    auto *outputType = ctx.create<NamedType>("Output", std::vector<Type *>{}, loc);
    auto *refMutOutputType = ctx.create<RefMutType>(outputType, loc);
    auto *indexMutMethod = ctx.create<FunctionDecl>(
        "index_mut", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, idxParam},
        refMutOutputType, nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem indexMutItem;
    indexMutItem.method = indexMutMethod;
    TraitItem outputAssoc;
    outputAssoc.assocTypeName = "Output";
    outputAssoc.isAssocType = true;
    GenericParam gp;
    gp.name = "Output";
    gp.loc = loc;
    auto *indexMutTrait = ctx.create<TraitDecl>(
        "IndexMut", std::vector<GenericParam>{gp},
        std::vector<Type *>{},
        std::vector<TraitItem>{outputAssoc, indexMutItem}, loc);
    traitDecls["IndexMut"] = indexMutTrait;
    Symbol sym;
    sym.name = "IndexMut";
    sym.decl = indexMutTrait;
    scope->declare("IndexMut", std::move(sym));
  }
}

} // namespace asc
