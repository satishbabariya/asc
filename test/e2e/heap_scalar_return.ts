// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "load i32, ptr" %t.out
// RUN: grep -q "ret i32" %t.out
// Test: @heap scalar variable is loaded before return, not returned as raw ptr.

fn main(): i32 {
  @heap
  let x: i32 = 42;
  return x;
}
