#ifndef ASC_CODEGEN_OWNERSHIPLOWERING_H
#define ASC_CODEGEN_OWNERSHIPLOWERING_H

#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include <memory>

namespace asc {

/// Ownership lowering pass — converts own.* and borrow.* ops to LLVM dialect.
/// Uses a simple walk-and-replace approach instead of the conversion framework
/// to avoid LLVM version-specific API incompatibilities.
std::unique_ptr<mlir::Pass> createOwnershipLoweringPass();

} // namespace asc

#endif // ASC_CODEGEN_OWNERSHIPLOWERING_H
