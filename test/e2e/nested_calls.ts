// Test: nested function calls and expressions.

function square(x: i32): i32 {
  return x * x;
}

function sum_of_squares(a: i32, b: i32): i32 {
  return square(a) + square(b);
}

function main(): i32 {
  return sum_of_squares(3, 4);
}
