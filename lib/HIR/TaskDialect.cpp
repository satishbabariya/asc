#include "asc/HIR/TaskDialect.h"
#include "asc/HIR/TaskOps.h"
#include "mlir/IR/DialectImplementation.h"

namespace asc {
namespace task {

TaskDialect::TaskDialect(mlir::MLIRContext *context)
    : mlir::Dialect("task", context, mlir::TypeID::get<TaskDialect>()) {
  addTypes<TaskHandleType, ChanTxType, ChanRxType>();
  addOperations<TaskSpawnOp, TaskJoinOp, ChanMakeOp, ChanSendOp, ChanRecvOp>();
}

mlir::Type TaskDialect::parseType(mlir::DialectAsmParser &parser) const {
  llvm::StringRef keyword;
  if (parser.parseKeyword(&keyword))
    return {};

  if (keyword == "handle") {
    if (parser.parseLess())
      return {};
    mlir::Type resultType;
    if (parser.parseType(resultType))
      return {};
    if (parser.parseGreater())
      return {};
    return TaskHandleType::get(getContext(), resultType);
  }
  if (keyword == "chan_tx") {
    if (parser.parseLess())
      return {};
    mlir::Type elemType;
    if (parser.parseType(elemType))
      return {};
    if (parser.parseGreater())
      return {};
    return ChanTxType::get(getContext(), elemType);
  }
  if (keyword == "chan_rx") {
    if (parser.parseLess())
      return {};
    mlir::Type elemType;
    if (parser.parseType(elemType))
      return {};
    if (parser.parseGreater())
      return {};
    return ChanRxType::get(getContext(), elemType);
  }

  parser.emitError(parser.getCurrentLocation(), "unknown task type: ") << keyword;
  return {};
}

void TaskDialect::printType(mlir::Type type,
                             mlir::DialectAsmPrinter &printer) const {
  if (auto th = mlir::dyn_cast<TaskHandleType>(type)) {
    printer << "handle<" << th.getResultType() << ">";
    return;
  }
  if (auto tx = mlir::dyn_cast<ChanTxType>(type)) {
    printer << "chan_tx<" << tx.getElementType() << ">";
    return;
  }
  if (auto rx = mlir::dyn_cast<ChanRxType>(type)) {
    printer << "chan_rx<" << rx.getElementType() << ">";
    return;
  }
}

// --- Type implementations ---

TaskHandleType TaskHandleType::get(mlir::MLIRContext *ctx,
                                    mlir::Type resultType) {
  return Base::get(ctx, resultType);
}

mlir::Type TaskHandleType::getResultType() const {
  return getImpl()->resultType;
}

ChanTxType ChanTxType::get(mlir::MLIRContext *ctx, mlir::Type elementType) {
  return Base::get(ctx, elementType);
}

mlir::Type ChanTxType::getElementType() const {
  return getImpl()->elementType;
}

ChanRxType ChanRxType::get(mlir::MLIRContext *ctx, mlir::Type elementType) {
  return Base::get(ctx, elementType);
}

mlir::Type ChanRxType::getElementType() const {
  return getImpl()->elementType;
}

} // namespace task
} // namespace asc
