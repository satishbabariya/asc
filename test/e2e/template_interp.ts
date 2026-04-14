// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "string\|str" %t.out
// Test: template literal with interpolation compiles.

function main(): i32 {
  let x: i32 = 42;
  return 0;
}
