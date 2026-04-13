#include "asc/HIR/OwnDialect.h"
#include "asc/HIR/OwnOps.h"
#include "asc/HIR/OwnTypes.h"

namespace asc {
namespace own {

OwnDialect::OwnDialect(mlir::MLIRContext *context)
    : mlir::Dialect("own", context, mlir::TypeID::get<OwnDialect>()) {
  addTypes<OwnValType, BorrowType, BorrowMutType>();
  addOperations<OwnAllocOp, OwnMoveOp, OwnDropOp, OwnCopyOp, BorrowRefOp,
                BorrowMutOp, OwnDropFlagAllocOp, OwnDropFlagSetOp,
                OwnDropFlagCheckOp>();
}

mlir::Type OwnDialect::parseType(mlir::DialectAsmParser &parser) const {
  llvm::StringRef keyword;
  if (parser.parseKeyword(&keyword))
    return {};
  if (keyword == "val")
    return OwnValType::get(getContext());
  if (keyword == "borrow")
    return BorrowType::get(getContext());
  if (keyword == "borrow_mut")
    return BorrowMutType::get(getContext());
  parser.emitError(parser.getCurrentLocation(), "unknown own type: ") << keyword;
  return {};
}

void OwnDialect::printType(mlir::Type type,
                            mlir::DialectAsmPrinter &printer) const {
  if (mlir::isa<OwnValType>(type)) { printer << "val"; return; }
  if (mlir::isa<BorrowType>(type)) { printer << "borrow"; return; }
  if (mlir::isa<BorrowMutType>(type)) { printer << "borrow_mut"; return; }
}

} // namespace own
} // namespace asc
