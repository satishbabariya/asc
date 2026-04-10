// RUN: %asc check %s
// Test generic function.

function identity<T>(x: own<T>): own<T> {
  return x;
}

function max(a: i32, b: i32): i32 {
  if a > b {
    return a;
  }
  return b;
}

function main(): i32 {
  const result = max(10, 20);
  return result;
}
