// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "store i32 2, ptr" %t.out
// RUN: grep -q "load i32, ptr" %t.out
// Test: @heap scalar mutation stores to the heap slot; subsequent read loads from it.

fn main(): i32 {
  @heap
  let x: i32 = 1;
  x = 2;
  return x;
}
