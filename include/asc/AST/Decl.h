#ifndef ASC_AST_DECL_H
#define ASC_AST_DECL_H

#include "asc/AST/Type.h"
#include "asc/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

namespace asc {

class Stmt;
class Expr;
class CompoundStmt;
class Pattern;

/// Discriminator for Decl subclasses.
enum class DeclKind {
  Function,
  Var,
  Struct,
  Enum,
  Trait,
  Impl,
  TypeAlias,
  Import,
  Export,
  Field,
  EnumVariant,
  Const,
  Static,
};

/// Generic parameter: `T: Bound1 + Bound2` or `const N: usize`
struct GenericParam {
  std::string name;
  std::vector<Type *> bounds;
  bool isConst = false;       // const generic
  Type *constType = nullptr;  // type for const generic
  SourceLocation loc;
};

/// Where clause constraint: `T: Bound1 + Bound2`
struct WhereConstraint {
  std::string typeName;
  std::vector<Type *> bounds;
  SourceLocation loc;
};

/// Base class for all declarations.
class Decl {
public:
  DeclKind getKind() const { return kind; }
  SourceLocation getLocation() const { return loc; }
  llvm::StringRef getName() const { return name; }

  const std::vector<std::string> &getAttributes() const { return attributes; }
  void addAttribute(std::string attr) { attributes.push_back(std::move(attr)); }

protected:
  Decl(DeclKind kind, std::string name, SourceLocation loc)
      : kind(kind), name(std::move(name)), loc(loc) {}

private:
  DeclKind kind;
  std::string name;
  SourceLocation loc;
  std::vector<std::string> attributes;
};

/// Function parameter.
struct ParamDecl {
  std::string name;
  Type *type = nullptr;
  bool isSelfRef = false;     // ref<Self>
  bool isSelfRefMut = false;  // refmut<Self>
  bool isSelfOwn = false;     // own<Self>
  SourceLocation loc;
};

/// Function declaration.
class FunctionDecl : public Decl {
public:
  FunctionDecl(std::string name, std::vector<GenericParam> genericParams,
               std::vector<ParamDecl> params, Type *returnType,
               CompoundStmt *body, std::vector<WhereConstraint> whereClause,
               SourceLocation loc)
      : Decl(DeclKind::Function, std::move(name), loc),
        genericParams(std::move(genericParams)),
        params(std::move(params)), returnType(returnType), body(body),
        whereClause(std::move(whereClause)) {}

  const std::vector<GenericParam> &getGenericParams() const {
    return genericParams;
  }
  const std::vector<ParamDecl> &getParams() const { return params; }
  Type *getReturnType() const { return returnType; }
  CompoundStmt *getBody() const { return body; }
  const std::vector<WhereConstraint> &getWhereClause() const {
    return whereClause;
  }

  bool isGeneric() const { return !genericParams.empty(); }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::Function;
  }

private:
  std::vector<GenericParam> genericParams;
  std::vector<ParamDecl> params;
  Type *returnType;
  CompoundStmt *body; // nullptr for trait method declarations
  std::vector<WhereConstraint> whereClause;
};

/// Variable declaration (const or let).
class VarDecl : public Decl {
public:
  VarDecl(std::string name, bool isConst, Type *type, Expr *init,
          Pattern *pattern, SourceLocation loc)
      : Decl(DeclKind::Var, std::move(name), loc), isConstBinding(isConst),
        type(type), init(init), pattern(pattern) {}

  bool isConst() const { return isConstBinding; }
  Type *getType() const { return type; }
  Expr *getInit() const { return init; }
  Pattern *getPattern() const { return pattern; }

  void setType(Type *t) { type = t; }

  static bool classof(const Decl *d) { return d->getKind() == DeclKind::Var; }

private:
  bool isConstBinding;
  Type *type;     // explicit type annotation, or nullptr
  Expr *init;     // initializer, or nullptr
  Pattern *pattern; // destructuring pattern, or nullptr (simple name)
};

/// Field declaration in a struct.
class FieldDecl : public Decl {
public:
  FieldDecl(std::string name, Type *type, SourceLocation loc)
      : Decl(DeclKind::Field, std::move(name), loc), type(type) {}

  Type *getType() const { return type; }

  static bool classof(const Decl *d) { return d->getKind() == DeclKind::Field; }

private:
  Type *type;
};

/// Struct declaration.
class StructDecl : public Decl {
public:
  StructDecl(std::string name, std::vector<GenericParam> genericParams,
             std::vector<FieldDecl *> fields, SourceLocation loc)
      : Decl(DeclKind::Struct, std::move(name), loc),
        genericParams(std::move(genericParams)),
        fields(std::move(fields)) {}

  const std::vector<GenericParam> &getGenericParams() const {
    return genericParams;
  }
  const std::vector<FieldDecl *> &getFields() const { return fields; }
  bool isUnit() const { return fields.empty(); }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::Struct;
  }

private:
  std::vector<GenericParam> genericParams;
  std::vector<FieldDecl *> fields;
};

/// Enum variant.
class EnumVariantDecl : public Decl {
public:
  enum class VariantKind { Unit, Tuple, Struct, Valued };

  EnumVariantDecl(std::string name, VariantKind variantKind,
                  std::vector<Type *> tupleTypes,
                  std::vector<FieldDecl *> structFields, Expr *value,
                  SourceLocation loc)
      : Decl(DeclKind::EnumVariant, std::move(name), loc),
        variantKind(variantKind), tupleTypes(std::move(tupleTypes)),
        structFields(std::move(structFields)), value(value) {}

  VariantKind getVariantKind() const { return variantKind; }
  const std::vector<Type *> &getTupleTypes() const { return tupleTypes; }
  const std::vector<FieldDecl *> &getStructFields() const {
    return structFields;
  }
  Expr *getValue() const { return value; }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::EnumVariant;
  }

private:
  VariantKind variantKind;
  std::vector<Type *> tupleTypes;
  std::vector<FieldDecl *> structFields;
  Expr *value; // for C-like enums: `Ok = 200`
};

/// Enum declaration.
class EnumDecl : public Decl {
public:
  EnumDecl(std::string name, std::vector<GenericParam> genericParams,
           std::vector<EnumVariantDecl *> variants, SourceLocation loc)
      : Decl(DeclKind::Enum, std::move(name), loc),
        genericParams(std::move(genericParams)),
        variants(std::move(variants)) {}

  const std::vector<GenericParam> &getGenericParams() const {
    return genericParams;
  }
  const std::vector<EnumVariantDecl *> &getVariants() const {
    return variants;
  }

  static bool classof(const Decl *d) { return d->getKind() == DeclKind::Enum; }

private:
  std::vector<GenericParam> genericParams;
  std::vector<EnumVariantDecl *> variants;
};

/// Trait item (method or associated type/const).
struct TraitItem {
  FunctionDecl *method = nullptr;   // method declaration
  // DECISION: Associated types/consts stored inline for simplicity.
  std::string assocTypeName;
  Type *assocTypeDefault = nullptr;
  bool isAssocType = false;
};

/// Trait declaration.
class TraitDecl : public Decl {
public:
  TraitDecl(std::string name, std::vector<GenericParam> genericParams,
            std::vector<Type *> supertraits, std::vector<TraitItem> items,
            SourceLocation loc)
      : Decl(DeclKind::Trait, std::move(name), loc),
        genericParams(std::move(genericParams)),
        supertraits(std::move(supertraits)), items(std::move(items)) {}

  const std::vector<GenericParam> &getGenericParams() const {
    return genericParams;
  }
  const std::vector<Type *> &getSupertraits() const { return supertraits; }
  const std::vector<TraitItem> &getItems() const { return items; }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::Trait;
  }

private:
  std::vector<GenericParam> genericParams;
  std::vector<Type *> supertraits;
  std::vector<TraitItem> items;
};

/// Impl block: `impl Type` or `impl Trait for Type`.
class ImplDecl : public Decl {
public:
  ImplDecl(std::vector<GenericParam> genericParams, Type *targetType,
           Type *traitType, std::vector<FunctionDecl *> methods,
           SourceLocation loc)
      : Decl(DeclKind::Impl, "", loc),
        genericParams(std::move(genericParams)), targetType(targetType),
        traitType(traitType), methods(std::move(methods)) {}

  const std::vector<GenericParam> &getGenericParams() const {
    return genericParams;
  }
  Type *getTargetType() const { return targetType; }
  Type *getTraitType() const { return traitType; } // nullptr if not trait impl
  const std::vector<FunctionDecl *> &getMethods() const { return methods; }
  bool isTraitImpl() const { return traitType != nullptr; }

  static bool classof(const Decl *d) { return d->getKind() == DeclKind::Impl; }

private:
  std::vector<GenericParam> genericParams;
  Type *targetType;
  Type *traitType;
  std::vector<FunctionDecl *> methods;
};

/// Type alias: `type Result<T> = std::Result<T, own<Error>>;`
class TypeAliasDecl : public Decl {
public:
  TypeAliasDecl(std::string name, std::vector<GenericParam> genericParams,
                Type *aliasedType, SourceLocation loc)
      : Decl(DeclKind::TypeAlias, std::move(name), loc),
        genericParams(std::move(genericParams)), aliasedType(aliasedType) {}

  const std::vector<GenericParam> &getGenericParams() const {
    return genericParams;
  }
  Type *getAliasedType() const { return aliasedType; }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::TypeAlias;
  }

private:
  std::vector<GenericParam> genericParams;
  Type *aliasedType;
};

/// Import specifier: a single name imported.
struct ImportSpecifier {
  std::string name;
  std::string alias; // empty if no alias
  bool isTypeOnly = false;
};

/// Import declaration: `import { X, Y } from 'path';`
class ImportDecl : public Decl {
public:
  ImportDecl(std::string modulePath, std::vector<ImportSpecifier> specifiers,
             SourceLocation loc)
      : Decl(DeclKind::Import, "", loc), modulePath(std::move(modulePath)),
        specifiers(std::move(specifiers)) {}

  llvm::StringRef getModulePath() const { return modulePath; }
  const std::vector<ImportSpecifier> &getSpecifiers() const {
    return specifiers;
  }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::Import;
  }

private:
  std::string modulePath;
  std::vector<ImportSpecifier> specifiers;
};

/// Export declaration.
class ExportDecl : public Decl {
public:
  ExportDecl(Decl *inner, SourceLocation loc)
      : Decl(DeclKind::Export, "", loc), inner(inner) {}

  Decl *getInner() const { return inner; }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::Export;
  }

private:
  Decl *inner; // the declaration being exported, or nullptr for re-exports
};

/// Const declaration: `const MAX: usize = 1024;`
class ConstDecl : public Decl {
public:
  ConstDecl(std::string name, Type *type, Expr *init, SourceLocation loc)
      : Decl(DeclKind::Const, std::move(name), loc), type(type), init(init) {}

  Type *getType() const { return type; }
  Expr *getInit() const { return init; }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::Const;
  }

private:
  Type *type;
  Expr *init;
};

/// Static declaration: `static COUNTER: AtomicI32 = AtomicI32::new(0);`
class StaticDecl : public Decl {
public:
  StaticDecl(std::string name, Type *type, Expr *init, bool isMut,
             SourceLocation loc)
      : Decl(DeclKind::Static, std::move(name), loc), type(type), init(init),
        isMutable(isMut) {}

  Type *getType() const { return type; }
  Expr *getInit() const { return init; }
  bool isMut() const { return isMutable; }

  static bool classof(const Decl *d) {
    return d->getKind() == DeclKind::Static;
  }

private:
  Type *type;
  Expr *init;
  bool isMutable;
};

} // namespace asc

#endif // ASC_AST_DECL_H
