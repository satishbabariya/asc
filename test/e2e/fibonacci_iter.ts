// RUN: %asc check %s
// Test: fibonacci with loop and mutable variables.

function fib(n: i32): i32 {
  if n <= 1 { return n; }
  let a: i32 = 0;
  let b: i32 = 1;
  let i: i32 = 2;
  while i <= n {
    let tmp: i32 = a + b;
    a = b;
    b = tmp;
    i = i + 1;
  }
  return b;
}

function main(): i32 {
  return fib(10);
}
