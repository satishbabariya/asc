// RUN: %asc check %s
// Test: Collection utility functions — validates module structure compiles.
function main(): i32 {
  // Collection utils are free functions in std/collections/utils.ts.
  // Full validation requires import support; this test validates parse correctness.
  let v: Vec<i32> = Vec::new();
  v.push(1);
  v.push(2);
  v.push(3);
  assert_eq!(v.len(), 3);
  return 0;
}
