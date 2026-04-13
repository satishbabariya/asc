// RUN: %asc build %s --emit llvmir > %t.out 2>&1; grep -q "__asc_get_panic_info\|__asc_panic" %t.out
// Test: panic infrastructure symbols are declared in LLVM IR.

struct Resource { id: i32 }

impl Drop for Resource {
  function drop(self: refmut<Resource>): void { }
}

function main(): i32 {
  let r = Resource { id: 1 };
  return 0;
}
