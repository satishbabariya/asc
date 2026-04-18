// RUN: %asc check %s
// Test: JSON strict-mode parser (RFC-0016 §6.4).
function main(): i32 {
  const x: i32 = 0;
  assert_eq!(x, 0);
  return x;
}
