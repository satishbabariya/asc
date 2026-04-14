// RUN: cp %s %t.ts && %asc fmt %t.ts
// Test: formatter runs without crashing on valid input.

function add(a: i32, b: i32): i32 {
  return a + b;
}

function main(): i32 {
  let x: i32 = add(1, 2);
  return x;
}
