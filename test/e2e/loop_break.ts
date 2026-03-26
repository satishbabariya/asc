// Test: infinite loop with break.

function count_to(target: i32): i32 {
  let x: i32 = 0;
  loop {
    if x >= target {
      break;
    }
    x = x + 1;
  }
  return x;
}

function main(): i32 {
  return count_to(42);
}
