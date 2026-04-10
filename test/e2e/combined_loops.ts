// RUN: %asc check %s
// Test: combined loops with break, continue, for-range, and arrays.

function matrix_diagonal_sum(): i32 {
  // Simulate a 3x3 matrix stored in a flat array
  let matrix = [1, 2, 3, 4, 5, 6, 7, 8, 9];
  let sum: i32 = 0;
  // Sum diagonal: matrix[0], matrix[4], matrix[8] = 1+5+9 = 15
  for (const i of 0..3) {
    let idx: i32 = i * 3 + i;
    sum = sum + matrix[idx];
  }
  return sum;
}

function collatz_steps(n: i32): i32 {
  let steps: i32 = 0;
  let x: i32 = n;
  loop {
    if x == 1 {
      break;
    }
    if x % 2 == 0 {
      x = x / 2;
    } else {
      x = x * 3 + 1;
    }
    steps = steps + 1;
  }
  return steps;
}

function sum_even_in_range(limit: i32): i32 {
  let total: i32 = 0;
  for (const i of 1..limit) {
    if i % 2 != 0 {
      continue;
    }
    total = total + i;
  }
  return total;
}

function main(): i32 {
  let a: i32 = matrix_diagonal_sum();
  let b: i32 = collatz_steps(6);
  let c: i32 = sum_even_in_range(11);
  // a=15, b=8 (6→3→10→5→16→8→4→2→1), c=2+4+6+8+10=30
  return a + b + c;
}
