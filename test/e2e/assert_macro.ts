// RUN: %asc check %s
// Test: assert! macro panics on false.

function main(): i32 {
  assert!(1 == 1);
  const x: i32 = 42;
  assert!(x > 0);
  return x;
}
