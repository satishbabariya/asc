// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "__asc_arena_alloc" %t.out
// Test: arena allocator runtime functions are available.

function main(): i32 {
  return 0;
}
