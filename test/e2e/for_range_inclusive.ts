// Test: inclusive range for loop (..=).

function sum_inclusive(n: i32): i32 {
  let total: i32 = 0;
  for (const i of 0..=n) {
    total = total + i;
  }
  return total;
}

function main(): i32 {
  return sum_inclusive(10);
}
