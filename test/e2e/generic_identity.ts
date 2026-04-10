// RUN: %asc check %s
// Test: generic function monomorphized at call site.

function identity<T>(x: own<T>): own<T> {
  return x;
}

function main(): i32 {
  const x: i32 = 42;
  return x;
}
