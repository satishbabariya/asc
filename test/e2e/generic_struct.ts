// RUN: %asc check %s
// Test: generic function with multiple type uses.

function first<T>(a: T, b: T): T {
  return a;
}

function second<T>(a: T, b: T): T {
  return b;
}

function main(): i32 {
  let a: i32 = first(10, 20);
  let b: i32 = second(30, 40);
  return a + b;
}
