#include "asc/HIR/OwnOps.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/IR/OpImplementation.h"

namespace asc {
namespace own {

//===----------------------------------------------------------------------===//
// OwnAllocOp
//===----------------------------------------------------------------------===//

void OwnAllocOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                        mlir::Type resultType, mlir::Value initValue) {
  state.addOperands(initValue);
  state.addTypes(resultType);
}

void OwnAllocOp::getEffects(
    llvm::SmallVectorImpl<
        mlir::SideEffects::EffectInstance<mlir::MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(mlir::MemoryEffects::Allocate::get());
}

mlir::ParseResult OwnAllocOp::parse(mlir::OpAsmParser &parser,
                                     mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand initOperand;
  mlir::Type resultType;
  if (parser.parseOperand(initOperand) || parser.parseColonType(resultType) ||
      parser.resolveOperand(initOperand, resultType, result.operands))
    return mlir::failure();
  result.addTypes(resultType);
  return mlir::success();
}

void OwnAllocOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getInitValue() << " : " << getResult().getType();
}

mlir::LogicalResult OwnAllocOp::verify() {
  // Result must be !own.val<T>.
  if (!mlir::isa<OwnValType>(getResult().getType()))
    return emitOpError("result must be !own.val<T>");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// OwnMoveOp
//===----------------------------------------------------------------------===//

void OwnMoveOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                       mlir::Value source) {
  state.addOperands(source);
  state.addTypes(source.getType());
}

mlir::ParseResult OwnMoveOp::parse(mlir::OpAsmParser &parser,
                                    mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand source;
  mlir::Type type;
  if (parser.parseOperand(source) || parser.parseColonType(type) ||
      parser.resolveOperand(source, type, result.operands))
    return mlir::failure();
  result.addTypes(type);
  return mlir::success();
}

void OwnMoveOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getSource() << " : " << getSource().getType();
}

mlir::LogicalResult OwnMoveOp::verify() {
  if (!mlir::isa<OwnValType>(getSource().getType()))
    return emitOpError("operand must be !own.val<T>");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// OwnDropOp
//===----------------------------------------------------------------------===//

void OwnDropOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                       mlir::Value ownedValue) {
  state.addOperands(ownedValue);
}

void OwnDropOp::getEffects(
    llvm::SmallVectorImpl<
        mlir::SideEffects::EffectInstance<mlir::MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(mlir::MemoryEffects::Free::get());
}

mlir::ParseResult OwnDropOp::parse(mlir::OpAsmParser &parser,
                                    mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand value;
  mlir::Type type;
  if (parser.parseOperand(value) || parser.parseColonType(type) ||
      parser.resolveOperand(value, type, result.operands))
    return mlir::failure();
  return mlir::success();
}

void OwnDropOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getOwnedValue() << " : " << getOwnedValue().getType();
}

mlir::LogicalResult OwnDropOp::verify() {
  if (!mlir::isa<OwnValType>(getOwnedValue().getType()))
    return emitOpError("operand must be !own.val<T>");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// OwnCopyOp
//===----------------------------------------------------------------------===//

void OwnCopyOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                       mlir::Value source) {
  state.addOperands(source);
  state.addTypes(source.getType());
}

mlir::ParseResult OwnCopyOp::parse(mlir::OpAsmParser &parser,
                                    mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand source;
  mlir::Type type;
  if (parser.parseOperand(source) || parser.parseColonType(type) ||
      parser.resolveOperand(source, type, result.operands))
    return mlir::failure();
  result.addTypes(type);
  return mlir::success();
}

void OwnCopyOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getSource() << " : " << getSource().getType();
}

mlir::LogicalResult OwnCopyOp::verify() {
  // DECISION: verifier checks @copy attribute during borrow check pass.
  if (!mlir::isa<OwnValType>(getSource().getType()))
    return emitOpError("operand must be !own.val<T>");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// BorrowRefOp
//===----------------------------------------------------------------------===//

void BorrowRefOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                         mlir::Value ownedValue) {
  auto ownType = mlir::cast<OwnValType>(ownedValue.getType());
  auto borrowType = BorrowType::get(builder.getContext(), ownType.getInnerType());
  state.addOperands(ownedValue);
  state.addTypes(borrowType);
}

mlir::ParseResult BorrowRefOp::parse(mlir::OpAsmParser &parser,
                                      mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand source;
  mlir::Type sourceType, resultType;
  if (parser.parseOperand(source) || parser.parseColonType(sourceType) ||
      parser.parseArrow() || parser.parseType(resultType) ||
      parser.resolveOperand(source, sourceType, result.operands))
    return mlir::failure();
  result.addTypes(resultType);
  return mlir::success();
}

void BorrowRefOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getOwnedValue() << " : " << getOwnedValue().getType() << " -> "
    << getResult().getType();
}

mlir::LogicalResult BorrowRefOp::verify() {
  if (!mlir::isa<OwnValType>(getOwnedValue().getType()))
    return emitOpError("source must be !own.val<T>");
  if (!mlir::isa<BorrowType>(getResult().getType()))
    return emitOpError("result must be !own.borrow<T>");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// BorrowMutOp
//===----------------------------------------------------------------------===//

void BorrowMutOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                         mlir::Value ownedValue) {
  auto ownType = mlir::cast<OwnValType>(ownedValue.getType());
  auto bmType = BorrowMutType::get(builder.getContext(), ownType.getInnerType());
  state.addOperands(ownedValue);
  state.addTypes(bmType);
}

mlir::ParseResult BorrowMutOp::parse(mlir::OpAsmParser &parser,
                                      mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand source;
  mlir::Type sourceType, resultType;
  if (parser.parseOperand(source) || parser.parseColonType(sourceType) ||
      parser.parseArrow() || parser.parseType(resultType) ||
      parser.resolveOperand(source, sourceType, result.operands))
    return mlir::failure();
  result.addTypes(resultType);
  return mlir::success();
}

void BorrowMutOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getOwnedValue() << " : " << getOwnedValue().getType() << " -> "
    << getResult().getType();
}

mlir::LogicalResult BorrowMutOp::verify() {
  if (!mlir::isa<OwnValType>(getOwnedValue().getType()))
    return emitOpError("source must be !own.val<T>");
  if (!mlir::isa<BorrowMutType>(getResult().getType()))
    return emitOpError("result must be !own.borrow_mut<T>");
  return mlir::success();
}

} // namespace own
} // namespace asc
