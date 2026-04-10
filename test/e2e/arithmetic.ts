// RUN: %asc check %s
// Test arithmetic operations through the full pipeline.

function add(a: i32, b: i32): i32 {
  return a + b;
}

function factorial(n: i32): i32 {
  if n <= 1 {
    return 1;
  }
  return n * factorial(n - 1);
}

function main(): i32 {
  const x: i32 = add(20, 22);
  return x;
}
