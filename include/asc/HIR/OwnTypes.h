#ifndef ASC_HIR_OWNTYPES_H
#define ASC_HIR_OWNTYPES_H

#include "mlir/IR/Types.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"

namespace asc {
namespace own {

namespace detail {
struct OwnValTypeStorage : public mlir::TypeStorage {
  using KeyTy = std::tuple<mlir::Type, bool, bool>;

  OwnValTypeStorage(mlir::Type innerType, bool isSend, bool isSync)
      : innerType(innerType), sendFlag(isSend), syncFlag(isSync) {}

  bool operator==(const KeyTy &key) const {
    return std::get<0>(key) == innerType &&
           std::get<1>(key) == sendFlag &&
           std::get<2>(key) == syncFlag;
  }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_combine(std::get<0>(key), std::get<1>(key),
                              std::get<2>(key));
  }

  static OwnValTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                       const KeyTy &key) {
    return new (allocator.allocate<OwnValTypeStorage>())
        OwnValTypeStorage(std::get<0>(key), std::get<1>(key),
                          std::get<2>(key));
  }

  mlir::Type innerType;
  bool sendFlag;
  bool syncFlag;
};
struct BorrowTypeStorage : public mlir::TypeStorage {
  using KeyTy = mlir::Type;

  BorrowTypeStorage(mlir::Type innerType) : innerType(innerType) {}

  bool operator==(const KeyTy &key) const { return key == innerType; }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_combine(key.getAsOpaquePointer());
  }

  static BorrowTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                       const KeyTy &key) {
    return new (allocator.allocate<BorrowTypeStorage>()) BorrowTypeStorage(key);
  }

  mlir::Type innerType;
};

struct BorrowMutTypeStorage : public mlir::TypeStorage {
  using KeyTy = mlir::Type;

  BorrowMutTypeStorage(mlir::Type innerType) : innerType(innerType) {}

  bool operator==(const KeyTy &key) const { return key == innerType; }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_combine(key.getAsOpaquePointer());
  }

  static BorrowMutTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                          const KeyTy &key) {
    return new (allocator.allocate<BorrowMutTypeStorage>()) BorrowMutTypeStorage(key);
  }

  mlir::Type innerType;
};
} // namespace detail

/// OwnValType: !own.val<T> — an owned value.
class OwnValType : public mlir::Type::TypeBase<OwnValType, mlir::Type,
                                                detail::OwnValTypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.val";

  static OwnValType get(mlir::MLIRContext *ctx, mlir::Type innerType = {},
                        bool isSend = true, bool isSync = false) {
    return Base::get(ctx, innerType, isSend, isSync);
  }

  mlir::Type getInnerType() const { return getImpl()->innerType; }
  bool isSend() const { return getImpl()->sendFlag; }
  bool isSync() const { return getImpl()->syncFlag; }
};

/// BorrowType: !own.borrow<T> — a shared borrow.
class BorrowType : public mlir::Type::TypeBase<BorrowType, mlir::Type,
                                                detail::BorrowTypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.borrow";

  static BorrowType get(mlir::MLIRContext *ctx, mlir::Type innerType = {}) {
    return Base::get(ctx, innerType);
  }

  mlir::Type getInnerType() const { return getImpl()->innerType; }
};

/// BorrowMutType: !own.borrow_mut<T> — an exclusive mutable borrow.
class BorrowMutType : public mlir::Type::TypeBase<BorrowMutType, mlir::Type,
                                                   detail::BorrowMutTypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.borrow_mut";

  static BorrowMutType get(mlir::MLIRContext *ctx,
                            mlir::Type innerType = {}) {
    return Base::get(ctx, innerType);
  }

  mlir::Type getInnerType() const { return getImpl()->innerType; }
};

} // namespace own
} // namespace asc

#endif // ASC_HIR_OWNTYPES_H
