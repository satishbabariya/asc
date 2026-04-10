// RUN: %asc check %s 2>&1 || true
// Utility module for import test.

export function add(a: i32, b: i32): i32 {
  return a + b;
}

export function mul(a: i32, b: i32): i32 {
  return a * b;
}
