// RUN: %asc check %s
// Test: for loop with break.

function find_in_range(target: i32): i32 {
  let result: i32 = -1;
  for (const i of 0..100) {
    if i == target {
      result = i;
      break;
    }
  }
  return result;
}

function main(): i32 {
  return find_in_range(25);
}
