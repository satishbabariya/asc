//===-- OwnDialect.td - Own dialect definition -----------*- tablegen -*---===//
//
// Defines the 'own' dialect for ownership and borrow lifecycle management.
//
//===----------------------------------------------------------------------===//

#ifndef ASC_HIR_OWN_DIALECT_TD
#define ASC_HIR_OWN_DIALECT_TD

include "mlir/IR/OpBase.td"

def OwnDialect : Dialect {
  let name = "own";
  let summary = "Ownership and borrow lifecycle dialect for asc";
  let description = [{
    The `own` dialect models ownership semantics including allocation,
    movement, dropping, copying, and borrowing of values. It enforces
    linear ownership: every `!own.val<T>` has exactly one consuming use.
  }];
  let cppNamespace = "::asc::own";
  let dependentDialects = [
    "mlir::arith::ArithDialect",
    "mlir::memref::MemRefDialect"
  ];
}

#endif // ASC_HIR_OWN_DIALECT_TD
