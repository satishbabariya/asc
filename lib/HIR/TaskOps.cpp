#include "asc/HIR/TaskOps.h"
#include "asc/HIR/OwnTypes.h"

namespace asc {
namespace task {

//===----------------------------------------------------------------------===//
// TaskSpawnOp
//===----------------------------------------------------------------------===//

void TaskSpawnOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                         mlir::Type handleType, mlir::FlatSymbolRefAttr callee,
                         mlir::ValueRange args) {
  state.addOperands(args);
  state.addAttribute("callee", callee);
  state.addTypes(handleType);
}

mlir::FlatSymbolRefAttr TaskSpawnOp::getCalleeAttr() {
  return (*this)->getAttrOfType<mlir::FlatSymbolRefAttr>("callee");
}

llvm::StringRef TaskSpawnOp::getCallee() { return getCalleeAttr().getValue(); }

mlir::ParseResult TaskSpawnOp::parse(mlir::OpAsmParser &parser,
                                      mlir::OperationState &result) {
  mlir::FlatSymbolRefAttr calleeAttr;
  if (parser.parseAttribute(calleeAttr, "callee", result.attributes))
    return mlir::failure();
  // Parse operands.
  llvm::SmallVector<mlir::OpAsmParser::UnresolvedOperand> operands;
  llvm::SmallVector<mlir::Type> operandTypes;
  if (parser.parseLParen())
    return mlir::failure();
  if (mlir::failed(parser.parseOptionalRParen())) {
    do {
      mlir::OpAsmParser::UnresolvedOperand operand;
      if (parser.parseOperand(operand))
        return mlir::failure();
      operands.push_back(operand);
    } while (mlir::succeeded(parser.parseOptionalComma()));
    if (parser.parseRParen())
      return mlir::failure();
  }
  // Parse types.
  if (parser.parseColon() || parser.parseLParen())
    return mlir::failure();
  if (mlir::failed(parser.parseOptionalRParen())) {
    do {
      mlir::Type type;
      if (parser.parseType(type))
        return mlir::failure();
      operandTypes.push_back(type);
    } while (mlir::succeeded(parser.parseOptionalComma()));
    if (parser.parseRParen())
      return mlir::failure();
  }
  if (parser.parseArrow())
    return mlir::failure();
  mlir::Type resultType;
  if (parser.parseType(resultType))
    return mlir::failure();
  result.addTypes(resultType);
  if (parser.resolveOperands(operands, operandTypes, parser.getNameLoc(),
                              result.operands))
    return mlir::failure();
  return mlir::success();
}

void TaskSpawnOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getCalleeAttr() << "(";
  p.printOperands(getArgOperands());
  p << ") : (";
  llvm::interleaveComma(getArgOperands().getTypes(), p);
  p << ") -> " << getResult().getType();
}

mlir::LogicalResult TaskSpawnOp::verify() {
  // Verify all captured values are Send.
  for (auto arg : getArgOperands()) {
    if (auto ownType = mlir::dyn_cast<own::OwnValType>(arg.getType())) {
      if (!ownType.isSend())
        return emitOpError("captured value is not Send");
    }
  }
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// TaskJoinOp
//===----------------------------------------------------------------------===//

void TaskJoinOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                        mlir::Value handle) {
  auto handleType = mlir::cast<TaskHandleType>(handle.getType());
  state.addOperands(handle);
  state.addTypes(handleType.getResultType());
}

mlir::ParseResult TaskJoinOp::parse(mlir::OpAsmParser &parser,
                                     mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand handle;
  mlir::Type handleType, resultType;
  if (parser.parseOperand(handle) || parser.parseColonType(handleType) ||
      parser.parseArrow() || parser.parseType(resultType) ||
      parser.resolveOperand(handle, handleType, result.operands))
    return mlir::failure();
  result.addTypes(resultType);
  return mlir::success();
}

void TaskJoinOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getHandle() << " : " << getHandle().getType() << " -> "
    << getResult().getType();
}

mlir::LogicalResult TaskJoinOp::verify() {
  if (!mlir::isa<TaskHandleType>(getHandle().getType()))
    return emitOpError("operand must be !task.handle<T>");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// ChanMakeOp
//===----------------------------------------------------------------------===//

void ChanMakeOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                        mlir::Type elementType, uint64_t capacity) {
  auto txType = ChanTxType::get(builder.getContext(), elementType);
  auto rxType = ChanRxType::get(builder.getContext(), elementType);
  state.addTypes({txType, rxType});
  state.addAttribute("capacity",
                      builder.getIntegerAttr(builder.getI64Type(), capacity));
}

uint64_t ChanMakeOp::getCapacity() {
  return (*this)
      ->getAttrOfType<mlir::IntegerAttr>("capacity")
      .getValue()
      .getZExtValue();
}

mlir::ParseResult ChanMakeOp::parse(mlir::OpAsmParser &parser,
                                     mlir::OperationState &result) {
  if (parser.parseOptionalAttrDict(result.attributes))
    return mlir::failure();
  mlir::Type txType, rxType;
  if (parser.parseColon() || parser.parseType(txType) || parser.parseComma() ||
      parser.parseType(rxType))
    return mlir::failure();
  result.addTypes({txType, rxType});
  return mlir::success();
}

void ChanMakeOp::print(mlir::OpAsmPrinter &p) {
  p.printOptionalAttrDict((*this)->getAttrs());
  p << " : " << getTx().getType() << ", " << getRx().getType();
}

mlir::LogicalResult ChanMakeOp::verify() { return mlir::success(); }

//===----------------------------------------------------------------------===//
// ChanSendOp
//===----------------------------------------------------------------------===//

void ChanSendOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                        mlir::Value tx, mlir::Value value) {
  state.addOperands({tx, value});
}

mlir::ParseResult ChanSendOp::parse(mlir::OpAsmParser &parser,
                                     mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand tx, value;
  mlir::Type txType, valType;
  if (parser.parseOperand(tx) || parser.parseComma() ||
      parser.parseOperand(value) || parser.parseColon() ||
      parser.parseType(txType) || parser.parseComma() ||
      parser.parseType(valType) ||
      parser.resolveOperand(tx, txType, result.operands) ||
      parser.resolveOperand(value, valType, result.operands))
    return mlir::failure();
  return mlir::success();
}

void ChanSendOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getTx() << ", " << getValue() << " : " << getTx().getType()
    << ", " << getValue().getType();
}

mlir::LogicalResult ChanSendOp::verify() {
  if (!mlir::isa<ChanTxType>(getTx().getType()))
    return emitOpError("first operand must be !task.chan_tx<T>");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// ChanRecvOp
//===----------------------------------------------------------------------===//

void ChanRecvOp::build(mlir::OpBuilder &builder, mlir::OperationState &state,
                        mlir::Value rx) {
  auto rxType = mlir::cast<ChanRxType>(rx.getType());
  auto valType =
      own::OwnValType::get(builder.getContext(), rxType.getElementType());
  state.addOperands(rx);
  state.addTypes(valType);
}

mlir::ParseResult ChanRecvOp::parse(mlir::OpAsmParser &parser,
                                     mlir::OperationState &result) {
  mlir::OpAsmParser::UnresolvedOperand rx;
  mlir::Type rxType, resultType;
  if (parser.parseOperand(rx) || parser.parseColonType(rxType) ||
      parser.parseArrow() || parser.parseType(resultType) ||
      parser.resolveOperand(rx, rxType, result.operands))
    return mlir::failure();
  result.addTypes(resultType);
  return mlir::success();
}

void ChanRecvOp::print(mlir::OpAsmPrinter &p) {
  p << " " << getRx() << " : " << getRx().getType() << " -> "
    << getResult().getType();
}

mlir::LogicalResult ChanRecvOp::verify() {
  if (!mlir::isa<ChanRxType>(getRx().getType()))
    return emitOpError("operand must be !task.chan_rx<T>");
  return mlir::success();
}

} // namespace task
} // namespace asc
