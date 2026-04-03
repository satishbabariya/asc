//===-- TaskDialect.td - Task dialect definition ----------*- tablegen -*---===//
//
// Defines the 'task' dialect for concurrency primitives.
//
//===----------------------------------------------------------------------===//

#ifndef ASC_HIR_TASK_DIALECT_TD
#define ASC_HIR_TASK_DIALECT_TD

include "mlir/IR/OpBase.td"

def TaskDialect : Dialect {
  let name = "task";
  let summary = "Concurrency primitives dialect for asc";
  let description = [{
    The `task` dialect provides operations for task-based concurrency
    and channel communication with ownership-based Send/Sync guarantees.
  }];
  let cppNamespace = "::asc::task";
  let dependentDialects = [];
}

#endif // ASC_HIR_TASK_DIALECT_TD
