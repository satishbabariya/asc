#ifndef ASC_HIR_TASKOPS_H
#define ASC_HIR_TASKOPS_H

#include "asc/HIR/TaskDialect.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"

namespace asc {
namespace task {

// DECISION: Task dialect types simplified to use default TypeStorage
// (no custom storage) to avoid LLVM 18 TypeBase API incompatibilities.
// Type parameters are carried on operations via attributes instead.

/// TaskHandleType: !task.handle — opaque handle to a spawned task.
class TaskHandleType
    : public mlir::Type::TypeBase<TaskHandleType, mlir::Type,
                                  mlir::TypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "task.handle";
  static TaskHandleType get(mlir::MLIRContext *ctx,
                            mlir::Type /*resultType*/ = {}) {
    return Base::get(ctx);
  }
  mlir::Type getResultType() const { return mlir::Type(); }
};

/// ChanTxType: !task.chan_tx — sending half of a channel.
class ChanTxType : public mlir::Type::TypeBase<ChanTxType, mlir::Type,
                                                mlir::TypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "task.chan_tx";
  static ChanTxType get(mlir::MLIRContext *ctx,
                        mlir::Type /*elementType*/ = {}) {
    return Base::get(ctx);
  }
  mlir::Type getElementType() const { return mlir::Type(); }
};

/// ChanRxType: !task.chan_rx — receiving half of a channel.
class ChanRxType : public mlir::Type::TypeBase<ChanRxType, mlir::Type,
                                                mlir::TypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "task.chan_rx";
  static ChanRxType get(mlir::MLIRContext *ctx,
                        mlir::Type /*elementType*/ = {}) {
    return Base::get(ctx);
  }
  mlir::Type getElementType() const { return mlir::Type(); }
};

// DECISION: Task dialect operations are declared but not used via
// Op<> template registration due to LLVM 18 compatibility issues with
// manually-defined ops (missing getAttributeNames, etc.).
// Instead, operations are created using generic mlir::Operation API
// with string names (e.g., "task.spawn") and the lowering passes
// match by name. This is the approach used by many out-of-tree dialects.

} // namespace task
} // namespace asc

#endif // ASC_HIR_TASKOPS_H
