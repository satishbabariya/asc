#include "asc/HIR/OwnDialect.h"
#include "asc/HIR/OwnOps.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/StringSwitch.h"

namespace asc {
namespace own {

OwnDialect::OwnDialect(mlir::MLIRContext *context)
    : mlir::Dialect("own", context, mlir::TypeID::get<OwnDialect>()) {
  addTypes<OwnValType, BorrowType, BorrowMutType>();
  addOperations<OwnAllocOp, OwnMoveOp, OwnDropOp, OwnCopyOp, BorrowRefOp,
                BorrowMutOp>();
}

mlir::Type OwnDialect::parseType(mlir::DialectAsmParser &parser) const {
  llvm::StringRef keyword;
  if (parser.parseKeyword(&keyword))
    return {};

  if (keyword == "val") {
    if (parser.parseLess())
      return {};
    mlir::Type inner;
    if (parser.parseType(inner))
      return {};
    bool send = true, sync = false;
    if (mlir::succeeded(parser.parseOptionalComma())) {
      llvm::StringRef attr;
      if (parser.parseKeyword(&attr))
        return {};
      send = (attr == "send");
      if (mlir::succeeded(parser.parseOptionalComma())) {
        if (parser.parseKeyword(&attr))
          return {};
        sync = (attr == "sync");
      }
    }
    if (parser.parseGreater())
      return {};
    return OwnValType::get(getContext(), inner, send, sync);
  }

  if (keyword == "borrow") {
    if (parser.parseLess())
      return {};
    mlir::Type inner;
    if (parser.parseType(inner))
      return {};
    if (parser.parseGreater())
      return {};
    return BorrowType::get(getContext(), inner);
  }

  if (keyword == "borrow_mut") {
    if (parser.parseLess())
      return {};
    mlir::Type inner;
    if (parser.parseType(inner))
      return {};
    if (parser.parseGreater())
      return {};
    return BorrowMutType::get(getContext(), inner);
  }

  parser.emitError(parser.getCurrentLocation(), "unknown own type: ") << keyword;
  return {};
}

void OwnDialect::printType(mlir::Type type,
                            mlir::DialectAsmPrinter &printer) const {
  if (auto ov = mlir::dyn_cast<OwnValType>(type)) {
    printer << "val<" << ov.getInnerType();
    if (!ov.isSend())
      printer << ", nosend";
    if (ov.isSync())
      printer << ", sync";
    printer << ">";
    return;
  }
  if (auto bt = mlir::dyn_cast<BorrowType>(type)) {
    printer << "borrow<" << bt.getInnerType() << ">";
    return;
  }
  if (auto bmt = mlir::dyn_cast<BorrowMutType>(type)) {
    printer << "borrow_mut<" << bmt.getInnerType() << ">";
    return;
  }
}

} // namespace own
} // namespace asc
