// RUN: %asc check %s
// Test: break and continue in while loops.

function find_first_over(limit: i32): i32 {
  let i: i32 = 0;
  while i < 100 {
    i = i + 1;
    if i > limit {
      break;
    }
  }
  return i;
}

function sum_skip_multiples_of_3(n: i32): i32 {
  let total: i32 = 0;
  let i: i32 = 0;
  while i < n {
    i = i + 1;
    if i % 3 == 0 {
      continue;
    }
    total = total + i;
  }
  return total;
}

function main(): i32 {
  let a: i32 = find_first_over(10);
  let b: i32 = sum_skip_multiples_of_3(10);
  // a = 11, b = 1+2+4+5+7+8+10 = 37
  return a + b;
}
