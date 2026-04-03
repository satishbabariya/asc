#ifndef ASC_CODEGEN_PANICLOWERING_H
#define ASC_CODEGEN_PANICLOWERING_H

#include "mlir/Pass/Pass.h"
#include <memory>

namespace asc {
std::unique_ptr<mlir::Pass> createPanicLoweringPass();
} // namespace asc

#endif
