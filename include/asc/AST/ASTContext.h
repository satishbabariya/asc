#ifndef ASC_AST_ASTCONTEXT_H
#define ASC_AST_ASTCONTEXT_H

#include "asc/AST/Type.h"
#include "llvm/Support/Allocator.h"
#include <cstddef>

namespace asc {

/// Owns all AST nodes via a bump allocator.
/// All nodes are allocated through this context and freed together.
class ASTContext {
public:
  ASTContext();
  ~ASTContext();

  /// Allocate memory for an AST node.
  void *allocate(size_t size, size_t alignment) {
    return allocator.Allocate(size, alignment);
  }

  /// Allocate and construct a node.
  template <typename T, typename... Args>
  T *create(Args &&...args) {
    void *mem = allocate(sizeof(T), alignof(T));
    return new (mem) T(std::forward<Args>(args)...);
  }

  /// Get cached builtin types.
  BuiltinType *getBuiltinType(BuiltinTypeKind kind);

  /// Get the void type.
  BuiltinType *getVoidType() { return getBuiltinType(BuiltinTypeKind::Void); }

  /// Get the bool type.
  BuiltinType *getBoolType() { return getBuiltinType(BuiltinTypeKind::Bool); }

  /// Get the i32 type.
  BuiltinType *getI32Type() { return getBuiltinType(BuiltinTypeKind::I32); }

  /// Get the total allocated bytes.
  size_t getAllocatedBytes() const { return allocator.getBytesAllocated(); }

private:
  llvm::BumpPtrAllocator allocator;
  // Cached builtin types (one per BuiltinTypeKind).
  BuiltinType *builtinTypes[18] = {};
};

} // namespace asc

#endif // ASC_AST_ASTCONTEXT_H
