#include "asc/HIR/OwnTypes.h"

// DECISION: OwnTypes use the simplified TypeBase<> without custom storage.
// All type parameter info (inner type, send, sync) is carried on operations
// via attributes. The type itself is a marker distinguishing owned values
// from borrows at the IR level.

namespace asc {
namespace own {
// All methods are inline in the header.
} // namespace own
} // namespace asc
