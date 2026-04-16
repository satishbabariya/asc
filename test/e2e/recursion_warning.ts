// RUN: %asc check %s
// Test: recursive function compiles without error.
function fib(n: i32): i32 {
  if n <= 1 { return n; }
  return fib(n - 1) + fib(n - 2);
}

function main(): i32 {
  return fib(10);
}
