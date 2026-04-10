// RUN: %asc check %s
// Test: if-else as expression.

function abs(x: i32): i32 {
  if x < 0 {
    return -x;
  } else {
    return x;
  }
}

function main(): i32 {
  return abs(-42);
}
