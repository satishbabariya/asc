// Tests that Box::new() allocates on heap via malloc.
// RUN: %asc build %s --emit llvmir > %t.out 2>&1; grep -q "malloc" %t.out

// Box::new() in HIRBuilder emits a direct malloc call, stores the value
// into the returned pointer, and returns it as !llvm.ptr.  The own.alloc
// op (stack allocation via alloca) is NOT involved in this path.

function main(): i32 {
  let b = Box::new(42);
  return 0;
}
