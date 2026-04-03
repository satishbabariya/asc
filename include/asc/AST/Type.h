#ifndef ASC_AST_TYPE_H
#define ASC_AST_TYPE_H

#include "asc/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

namespace asc {

/// Type kind discriminator.
enum class TypeKind {
  Builtin,
  Named,
  Own,
  Ref,
  RefMut,
  Array,
  Slice,
  Tuple,
  Function,
  DynTrait,
  Nullable,
  Generic,
  Inferred, // placeholder for type inference
  Path,
};

/// Built-in primitive type kind.
enum class BuiltinTypeKind {
  I8, I16, I32, I64, I128,
  U8, U16, U32, U64, U128,
  F32, F64,
  Bool, Char,
  USize, ISize,
  Void, Never,
};

class ASTContext;

/// Base class for all types in the AST.
class Type {
public:
  virtual ~Type() = default;
  TypeKind getKind() const { return kind; }
  SourceLocation getLocation() const { return loc; }

  bool isBuiltin() const { return kind == TypeKind::Builtin; }
  bool isOwn() const { return kind == TypeKind::Own; }
  bool isRef() const { return kind == TypeKind::Ref; }
  bool isRefMut() const { return kind == TypeKind::RefMut; }

protected:
  Type(TypeKind kind, SourceLocation loc) : kind(kind), loc(loc) {}

private:
  TypeKind kind;
  SourceLocation loc;
};

/// Primitive types: i32, f64, bool, etc.
class BuiltinType : public Type {
public:
  BuiltinType(BuiltinTypeKind btk, SourceLocation loc)
      : Type(TypeKind::Builtin, loc), builtinKind(btk) {}

  BuiltinTypeKind getBuiltinKind() const { return builtinKind; }

  bool isSigned() const {
    return builtinKind == BuiltinTypeKind::I8 ||
           builtinKind == BuiltinTypeKind::I16 ||
           builtinKind == BuiltinTypeKind::I32 ||
           builtinKind == BuiltinTypeKind::I64 ||
           builtinKind == BuiltinTypeKind::I128 ||
           builtinKind == BuiltinTypeKind::ISize;
  }

  bool isUnsigned() const {
    return builtinKind == BuiltinTypeKind::U8 ||
           builtinKind == BuiltinTypeKind::U16 ||
           builtinKind == BuiltinTypeKind::U32 ||
           builtinKind == BuiltinTypeKind::U64 ||
           builtinKind == BuiltinTypeKind::U128 ||
           builtinKind == BuiltinTypeKind::USize;
  }

  bool isInteger() const { return isSigned() || isUnsigned(); }
  bool isFloat() const {
    return builtinKind == BuiltinTypeKind::F32 ||
           builtinKind == BuiltinTypeKind::F64;
  }
  bool isBool() const { return builtinKind == BuiltinTypeKind::Bool; }
  bool isVoid() const { return builtinKind == BuiltinTypeKind::Void; }
  bool isNever() const { return builtinKind == BuiltinTypeKind::Never; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Builtin; }

private:
  BuiltinTypeKind builtinKind;
};

/// A named type reference: `Point`, `Vec`, etc.
class NamedType : public Type {
public:
  NamedType(std::string name, std::vector<Type *> genericArgs, SourceLocation loc)
      : Type(TypeKind::Named, loc), name(std::move(name)),
        genericArgs(std::move(genericArgs)) {}

  llvm::StringRef getName() const { return name; }
  const std::vector<Type *> &getGenericArgs() const { return genericArgs; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Named; }

private:
  std::string name;
  std::vector<Type *> genericArgs;
};

/// Path type: `Foo::Bar::Baz<T>`
class PathType : public Type {
public:
  PathType(std::vector<std::string> segments, std::vector<Type *> genericArgs,
           SourceLocation loc)
      : Type(TypeKind::Path, loc), segments(std::move(segments)),
        genericArgs(std::move(genericArgs)) {}

  const std::vector<std::string> &getSegments() const { return segments; }
  const std::vector<Type *> &getGenericArgs() const { return genericArgs; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Path; }

private:
  std::vector<std::string> segments;
  std::vector<Type *> genericArgs;
};

/// own<T>
class OwnType : public Type {
public:
  OwnType(Type *inner, SourceLocation loc)
      : Type(TypeKind::Own, loc), inner(inner) {}

  Type *getInner() const { return inner; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Own; }

private:
  Type *inner;
};

/// ref<T>
class RefType : public Type {
public:
  RefType(Type *inner, SourceLocation loc)
      : Type(TypeKind::Ref, loc), inner(inner) {}

  Type *getInner() const { return inner; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Ref; }

private:
  Type *inner;
};

/// refmut<T>
class RefMutType : public Type {
public:
  RefMutType(Type *inner, SourceLocation loc)
      : Type(TypeKind::RefMut, loc), inner(inner) {}

  Type *getInner() const { return inner; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::RefMut; }

private:
  Type *inner;
};

/// [T; N] — fixed-size array
class ArrayType : public Type {
public:
  ArrayType(Type *element, uint64_t size, SourceLocation loc)
      : Type(TypeKind::Array, loc), element(element), size(size) {}

  Type *getElementType() const { return element; }
  uint64_t getSize() const { return size; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Array; }

private:
  Type *element;
  uint64_t size;
};

/// [T] — slice type
class SliceType : public Type {
public:
  SliceType(Type *element, SourceLocation loc)
      : Type(TypeKind::Slice, loc), element(element) {}

  Type *getElementType() const { return element; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Slice; }

private:
  Type *element;
};

/// (T1, T2, ...) — tuple type
class TupleType : public Type {
public:
  TupleType(std::vector<Type *> elements, SourceLocation loc)
      : Type(TypeKind::Tuple, loc), elements(std::move(elements)) {}

  const std::vector<Type *> &getElements() const { return elements; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Tuple; }

private:
  std::vector<Type *> elements;
};

/// Function type: (T1, T2) -> R
class FunctionType : public Type {
public:
  FunctionType(std::vector<Type *> params, Type *returnType, SourceLocation loc)
      : Type(TypeKind::Function, loc), params(std::move(params)),
        returnType(returnType) {}

  const std::vector<Type *> &getParamTypes() const { return params; }
  Type *getReturnType() const { return returnType; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Function; }

private:
  std::vector<Type *> params;
  Type *returnType;
};

/// dyn Trait (+ Trait2)
class DynTraitType : public Type {
public:
  struct TraitBound {
    std::string name;
    std::vector<Type *> genericArgs;
  };

  DynTraitType(std::vector<TraitBound> bounds, SourceLocation loc)
      : Type(TypeKind::DynTrait, loc), bounds(std::move(bounds)) {}

  const std::vector<TraitBound> &getBounds() const { return bounds; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::DynTrait; }

private:
  std::vector<TraitBound> bounds;
};

/// T | null (nullable type)
class NullableType : public Type {
public:
  NullableType(Type *inner, SourceLocation loc)
      : Type(TypeKind::Nullable, loc), inner(inner) {}

  Type *getInner() const { return inner; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Nullable; }

private:
  Type *inner;
};

/// Placeholder type for when type inference hasn't resolved yet.
class InferredType : public Type {
public:
  explicit InferredType(SourceLocation loc)
      : Type(TypeKind::Inferred, loc) {}

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Inferred; }
};

/// Generic type parameter: `T` with optional bounds.
class GenericType : public Type {
public:
  struct Bound {
    std::string traitName;
    std::vector<Type *> genericArgs;
  };

  GenericType(std::string name, std::vector<Bound> bounds, SourceLocation loc)
      : Type(TypeKind::Generic, loc), name(std::move(name)),
        bounds(std::move(bounds)) {}

  llvm::StringRef getName() const { return name; }
  const std::vector<Bound> &getBounds() const { return bounds; }

  static bool classof(const Type *t) { return t->getKind() == TypeKind::Generic; }

private:
  std::string name;
  std::vector<Bound> bounds;
};

} // namespace asc

#endif // ASC_AST_TYPE_H
