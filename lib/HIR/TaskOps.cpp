#include "asc/HIR/TaskOps.h"

namespace asc {
namespace task {
// DECISION: Task ops are created via generic OperationState in HIR builder
// and matched by name string in lowering passes. No custom build/verify
// methods needed here.
} // namespace task
} // namespace asc
