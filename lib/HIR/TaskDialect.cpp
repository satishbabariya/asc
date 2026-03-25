#include "asc/HIR/TaskDialect.h"
#include "asc/HIR/TaskOps.h"

namespace asc {
namespace task {

TaskDialect::TaskDialect(mlir::MLIRContext *context)
    : mlir::Dialect("task", context, mlir::TypeID::get<TaskDialect>()) {
  addTypes<TaskHandleType, ChanTxType, ChanRxType>();
}

mlir::Type TaskDialect::parseType(mlir::DialectAsmParser &parser) const {
  llvm::StringRef keyword;
  if (parser.parseKeyword(&keyword))
    return {};
  if (keyword == "handle")
    return TaskHandleType::get(getContext());
  if (keyword == "chan_tx")
    return ChanTxType::get(getContext());
  if (keyword == "chan_rx")
    return ChanRxType::get(getContext());
  parser.emitError(parser.getCurrentLocation(), "unknown task type: ") << keyword;
  return {};
}

void TaskDialect::printType(mlir::Type type,
                             mlir::DialectAsmPrinter &printer) const {
  if (mlir::isa<TaskHandleType>(type)) { printer << "handle"; return; }
  if (mlir::isa<ChanTxType>(type)) { printer << "chan_tx"; return; }
  if (mlir::isa<ChanRxType>(type)) { printer << "chan_rx"; return; }
}

} // namespace task
} // namespace asc
