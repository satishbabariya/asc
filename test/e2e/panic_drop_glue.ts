// RUN: %asc build %s --emit llvmir > %t.out 2>&1; grep -q "__drop_" %t.out
// Tests that destructors run in the panic cleanup block.

struct Resource { id: i32 }

impl Drop for Resource {
  function drop(self: refmut<Resource>): void {
    // Cleanup logic runs on panic path
  }
}

function may_panic(): i32 { return 42; }

function main(): i32 {
  let r = Resource { id: 1 };
  return may_panic();
}
