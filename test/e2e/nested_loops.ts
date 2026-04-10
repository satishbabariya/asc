// RUN: %asc check %s
// Test: nested loops with break affecting only inner loop.

function find_pair_sum(target: i32): i32 {
  let result: i32 = 0;
  let i: i32 = 1;
  while i <= 10 {
    let j: i32 = 1;
    while j <= 10 {
      if i + j == target {
        result = i * 100 + j;
        break;
      }
      j = j + 1;
    }
    if result > 0 {
      break;
    }
    i = i + 1;
  }
  return result;
}

function main(): i32 {
  // find_pair_sum(7): first pair where i+j=7 is i=1,j=6 → 106
  let r: i32 = find_pair_sum(7);
  return r;
}
