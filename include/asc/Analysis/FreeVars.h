#ifndef ASC_ANALYSIS_FREEVARS_H
#define ASC_ANALYSIS_FREEVARS_H

#include "asc/AST/Expr.h"
#include "llvm/ADT/StringSet.h"

namespace asc {

/// Collect identifiers referenced inside `expr` that are not bound by
/// `boundNames` (closure params + inner `let`s). Used by both Sema (to
/// validate Send on captures) and HIRBuilder (to synthesize env structs).
void collectFreeVars(Expr *expr,
                     const llvm::StringSet<> &boundNames,
                     llvm::StringSet<> &freeVars);

} // namespace asc

#endif
