// RUN: %asc build %s --target aarch64-apple-darwin --emit obj -o %t.o
// Tests that native target compilation works at default O2.

function fib(n: i32): i32 {
  if (n <= 1) { return n; }
  return fib(n - 1) + fib(n - 2);
}

function main(): i32 {
  return fib(10);
}
