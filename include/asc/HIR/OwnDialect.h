#ifndef ASC_HIR_OWNDIALECT_H
#define ASC_HIR_OWNDIALECT_H

#include "mlir/IR/Dialect.h"

namespace asc {
namespace own {

//===----------------------------------------------------------------------===//
// OwnDialect
//
// The "own" MLIR dialect for the asc compiler's ownership model.
// Defines types: !own.val<T>, !own.borrow<T>, !own.borrow_mut<T>
// Defines ops:   own.alloc, own.move, own.drop, own.copy,
//                own.borrow_ref, own.borrow_mut
//===----------------------------------------------------------------------===//
class OwnDialect : public mlir::Dialect {
public:
  explicit OwnDialect(mlir::MLIRContext *context);

  /// Dialect namespace.
  static llvm::StringRef getDialectNamespace() { return "own"; }

  /// Parse a type registered to this dialect.
  mlir::Type parseType(mlir::DialectAsmParser &parser) const override;

  /// Print a type registered to this dialect.
  void printType(mlir::Type type,
                 mlir::DialectAsmPrinter &printer) const override;
};

} // namespace own
} // namespace asc

#endif // ASC_HIR_OWNDIALECT_H
