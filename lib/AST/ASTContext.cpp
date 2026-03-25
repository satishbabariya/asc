#include "asc/AST/ASTContext.h"

namespace asc {

ASTContext::ASTContext() = default;
ASTContext::~ASTContext() = default;

BuiltinType *ASTContext::getBuiltinType(BuiltinTypeKind kind) {
  unsigned idx = static_cast<unsigned>(kind);
  if (!builtinTypes[idx]) {
    builtinTypes[idx] = create<BuiltinType>(kind, SourceLocation());
  }
  return builtinTypes[idx];
}

} // namespace asc
