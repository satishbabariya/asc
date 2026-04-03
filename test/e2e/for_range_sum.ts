// Test: for loop with range, summing values.

function sum_range(n: i32): i32 {
  let total: i32 = 0;
  for (const i of 0..n) {
    total = total + i;
  }
  return total;
}

function main(): i32 {
  return sum_range(10);
}
