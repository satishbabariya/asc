// RUN: %asc check %s
// Test: deeply nested if/else chains.

function classify(x: i32): i32 {
  if x < 0 {
    return -1;
  } else {
    if x == 0 {
      return 0;
    } else {
      if x < 10 {
        return 1;
      } else {
        if x < 100 {
          return 2;
        } else {
          return 3;
        }
      }
    }
  }
}

function main(): i32 {
  let a: i32 = classify(-5);
  let b: i32 = classify(0);
  let c: i32 = classify(7);
  let d: i32 = classify(42);
  let e: i32 = classify(999);
  // -1 + 0 + 1 + 2 + 3 = 5
  return a + b + c + d + e;
}
