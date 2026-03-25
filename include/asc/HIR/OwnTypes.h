#ifndef ASC_HIR_OWNTYPES_H
#define ASC_HIR_OWNTYPES_H

#include "mlir/IR/Types.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"

namespace asc {
namespace own {

// DECISION: Use simple MLIR type aliases instead of custom TypeStorage.
// This avoids LLVM version-specific TypeBase<> API incompatibilities.
// The ownership semantics (Send, Sync) are tracked via attributes
// on operations rather than embedded in the type.

/// OwnValType: !own.val<T> — an owned value.
/// Implemented as a simple wrapper using MLIR's type mechanism.
class OwnValType : public mlir::Type::TypeBase<OwnValType, mlir::Type,
                                                mlir::TypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.val";

  static OwnValType get(mlir::MLIRContext *ctx) {
    return Base::get(ctx);
  }
  // Overloaded convenience: ignore inner type/send/sync (carried via attributes).
  static OwnValType get(mlir::MLIRContext *ctx, mlir::Type /*innerType*/,
                        bool /*send*/ = true, bool /*sync*/ = false) {
    return Base::get(ctx);
  }

  mlir::Type getInnerType() const { return mlir::Type(); }
  bool isSend() const { return true; }
  bool isSync() const { return false; }
};

/// BorrowType: !own.borrow<T> — a shared borrow.
class BorrowType : public mlir::Type::TypeBase<BorrowType, mlir::Type,
                                                mlir::TypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.borrow";

  static BorrowType get(mlir::MLIRContext *ctx, mlir::Type innerType = {}) {
    return Base::get(ctx);
  }

  mlir::Type getInnerType() const { return mlir::Type(); }
};

/// BorrowMutType: !own.borrow_mut<T> — an exclusive mutable borrow.
class BorrowMutType : public mlir::Type::TypeBase<BorrowMutType, mlir::Type,
                                                   mlir::TypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.borrow_mut";

  static BorrowMutType get(mlir::MLIRContext *ctx,
                            mlir::Type innerType = {}) {
    return Base::get(ctx);
  }

  mlir::Type getInnerType() const { return mlir::Type(); }
};

} // namespace own
} // namespace asc

#endif // ASC_HIR_OWNTYPES_H
