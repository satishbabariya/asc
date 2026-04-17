// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "load i32, ptr" %t.out
// RUN: grep -q "call.*@double.*i32" %t.out
// Test: @heap scalar passed as function argument is loaded at the call site.

fn double_it(x: i32): i32 {
  return x * 2;
}

fn main(): i32 {
  @heap
  let x: i32 = 21;
  return double_it(x);
}
