// RUN: %asc check %s
function fib(n: i32): i32 {
  if n <= 1 { return n; }
  let a: i32 = 0; let b: i32 = 1; let i: i32 = 2;
  while i <= n { let tmp: i32 = a + b; a = b; b = tmp; i = i + 1; }
  return b;
}
function main(): i32 { if fib(20) == 6765 { return 0; } return 1; }
