#include "asc/HIR/OwnOps.h"
#include "asc/HIR/OwnTypes.h"

namespace asc {
namespace own {

void OwnAllocOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                        mlir::Type resultType, mlir::Value initValue) {
  state.addOperands(initValue);
  state.addTypes(resultType);
}

void OwnMoveOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                       mlir::Value source) {
  state.addOperands(source);
  state.addTypes(source.getType());
}

void OwnDropOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                       mlir::Value ownedValue) {
  state.addOperands(ownedValue);
}

void OwnCopyOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                       mlir::Value source) {
  state.addOperands(source);
  state.addTypes(source.getType());
}

void BorrowRefOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                         mlir::Value ownedValue) {
  state.addOperands(ownedValue);
  state.addTypes(BorrowType::get(builder.getContext()));
}

void BorrowMutOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                         mlir::Value ownedValue) {
  state.addOperands(ownedValue);
  state.addTypes(BorrowMutType::get(builder.getContext()));
}

} // namespace own
} // namespace asc
