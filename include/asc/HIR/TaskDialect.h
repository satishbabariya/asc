#ifndef ASC_HIR_TASKDIALECT_H
#define ASC_HIR_TASKDIALECT_H

#include "mlir/IR/Dialect.h"

namespace asc {
namespace task {

//===----------------------------------------------------------------------===//
// TaskDialect
//
// The "task" MLIR dialect for asc's concurrency model.
// Defines types: !task.handle, !task.chan_tx<T>, !task.chan_rx<T>
// Defines ops:   task.spawn, task.join, task.chan_make,
//                task.chan_send, task.chan_recv
//===----------------------------------------------------------------------===//
class TaskDialect : public mlir::Dialect {
public:
  explicit TaskDialect(mlir::MLIRContext *context);

  /// Dialect namespace.
  static llvm::StringRef getDialectNamespace() { return "task"; }

  /// Parse a type registered to this dialect.
  mlir::Type parseType(mlir::DialectAsmParser &parser) const override;

  /// Print a type registered to this dialect.
  void printType(mlir::Type type,
                 mlir::DialectAsmPrinter &printer) const override;
};

} // namespace task
} // namespace asc

#endif // ASC_HIR_TASKDIALECT_H
