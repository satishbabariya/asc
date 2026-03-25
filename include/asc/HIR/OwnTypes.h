#ifndef ASC_HIR_OWNTYPES_H
#define ASC_HIR_OWNTYPES_H

#include "mlir/IR/Types.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeID.h"

namespace asc {
namespace own {

//===----------------------------------------------------------------------===//
// OwnValType: !own.val<inner_type, send, sync>
//
// Represents an owned value with Send/Sync capabilities.
// Maps to the `own<T>` surface syntax.
//===----------------------------------------------------------------------===//
class OwnValType : public mlir::Type::TypeBase<OwnValType, mlir::Type,
                                                 mlir::TypeStorage> {
public:
  /// Storage class for OwnValType.
  struct Storage : public mlir::TypeStorage {
    Storage(mlir::Type innerType, bool send, bool sync)
        : innerType(innerType), send(send), sync(sync) {}

    using KeyTy = std::tuple<mlir::Type, bool, bool>;

    bool operator==(const KeyTy &key) const {
      return std::get<0>(key) == innerType && std::get<1>(key) == send &&
             std::get<2>(key) == sync;
    }

    static llvm::hash_code hashKey(const KeyTy &key) {
      return llvm::hash_combine(std::get<0>(key), std::get<1>(key),
                                std::get<2>(key));
    }

    static Storage *construct(mlir::TypeStorageAllocator &allocator,
                              const KeyTy &key) {
      return new (allocator.allocate<Storage>())
          Storage(std::get<0>(key), std::get<1>(key), std::get<2>(key));
    }

    mlir::Type innerType;
    bool send;
    bool sync;
  };

  using Base = mlir::Type::TypeBase<OwnValType, mlir::Type, Storage>;
  using Base::Base;

  /// Get an OwnValType instance.
  static OwnValType get(mlir::MLIRContext *ctx, mlir::Type innerType,
                        bool send = true, bool sync = false);

  /// Get the inner (pointee) type.
  mlir::Type getInnerType() const;

  /// Whether this owned value is Send (can be transferred across threads).
  bool isSend() const;

  /// Whether this owned value is Sync (safe to share references across threads).
  bool isSync() const;
};

//===----------------------------------------------------------------------===//
// BorrowType: !own.borrow<inner_type>
//
// A shared (immutable) borrow of a value. Maps to `ref<T>`.
//===----------------------------------------------------------------------===//
class BorrowType : public mlir::Type::TypeBase<BorrowType, mlir::Type,
                                                mlir::TypeStorage> {
public:
  struct Storage : public mlir::TypeStorage {
    Storage(mlir::Type innerType) : innerType(innerType) {}

    using KeyTy = mlir::Type;

    bool operator==(const KeyTy &key) const { return key == innerType; }

    static llvm::hash_code hashKey(const KeyTy &key) {
      return llvm::hash_value(key);
    }

    static Storage *construct(mlir::TypeStorageAllocator &allocator,
                              const KeyTy &key) {
      return new (allocator.allocate<Storage>()) Storage(key);
    }

    mlir::Type innerType;
  };

  using Base = mlir::Type::TypeBase<BorrowType, mlir::Type, Storage>;
  using Base::Base;

  /// Get a BorrowType instance.
  static BorrowType get(mlir::MLIRContext *ctx, mlir::Type innerType);

  /// Get the inner (pointee) type.
  mlir::Type getInnerType() const;
};

//===----------------------------------------------------------------------===//
// BorrowMutType: !own.borrow_mut<inner_type>
//
// An exclusive (mutable) borrow of a value. Maps to `refmut<T>`.
//===----------------------------------------------------------------------===//
class BorrowMutType
    : public mlir::Type::TypeBase<BorrowMutType, mlir::Type,
                                  mlir::TypeStorage> {
public:
  struct Storage : public mlir::TypeStorage {
    Storage(mlir::Type innerType) : innerType(innerType) {}

    using KeyTy = mlir::Type;

    bool operator==(const KeyTy &key) const { return key == innerType; }

    static llvm::hash_code hashKey(const KeyTy &key) {
      return llvm::hash_value(key);
    }

    static Storage *construct(mlir::TypeStorageAllocator &allocator,
                              const KeyTy &key) {
      return new (allocator.allocate<Storage>()) Storage(key);
    }

    mlir::Type innerType;
  };

  using Base = mlir::Type::TypeBase<BorrowMutType, mlir::Type, Storage>;
  using Base::Base;

  /// Get a BorrowMutType instance.
  static BorrowMutType get(mlir::MLIRContext *ctx, mlir::Type innerType);

  /// Get the inner (pointee) type.
  mlir::Type getInnerType() const;
};

} // namespace own
} // namespace asc

#endif // ASC_HIR_OWNTYPES_H
