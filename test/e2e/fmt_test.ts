// RUN: cp %s %t.ts && %asc fmt %t.ts
// Test: asc fmt normalizes indentation.
function main(): i32 {
  const x: i32 = 42;
  return x;
}
