// RUN: %asc check %s
// Test: while loop.

function sum_to(n: i32): i32 {
  let result: i32 = 0;
  let i: i32 = 1;
  while i <= n {
    result = result + i;
    i = i + 1;
  }
  return result;
}

function main(): i32 {
  return sum_to(10);
}
