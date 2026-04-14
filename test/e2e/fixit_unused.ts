// RUN: %asc check %s > %t.out 2>&1; grep -q "W005" %t.out
// Test: unused variable produces a W005 warning.

function main(): i32 {
  let unused_var: i32 = 42;
  return 0;
}
