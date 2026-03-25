//===- OwnTypes.cpp - Own dialect type implementations --------------------===//
//
// Implements the MLIR types for the asc ownership dialect:
//   !own.val<T, send, sync>  — owned value
//   !own.borrow<T>           — shared borrow
//   !own.borrow_mut<T>       — exclusive mutable borrow
//
//===----------------------------------------------------------------------===//

#include "asc/HIR/OwnTypes.h"
#include "asc/HIR/OwnDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

using namespace asc::own;

//===----------------------------------------------------------------------===//
// OwnValType
//===----------------------------------------------------------------------===//

OwnValType OwnValType::get(mlir::MLIRContext *ctx, mlir::Type innerType,
                            bool send, bool sync) {
  return Base::get(ctx, innerType, send, sync);
}

mlir::Type OwnValType::getInnerType() const { return getImpl()->innerType; }

bool OwnValType::isSend() const { return getImpl()->send; }

bool OwnValType::isSync() const { return getImpl()->sync; }

//===----------------------------------------------------------------------===//
// BorrowType
//===----------------------------------------------------------------------===//

BorrowType BorrowType::get(mlir::MLIRContext *ctx, mlir::Type innerType) {
  return Base::get(ctx, innerType);
}

mlir::Type BorrowType::getInnerType() const { return getImpl()->innerType; }

//===----------------------------------------------------------------------===//
// BorrowMutType
//===----------------------------------------------------------------------===//

BorrowMutType BorrowMutType::get(mlir::MLIRContext *ctx,
                                  mlir::Type innerType) {
  return Base::get(ctx, innerType);
}

mlir::Type BorrowMutType::getInnerType() const {
  return getImpl()->innerType;
}
