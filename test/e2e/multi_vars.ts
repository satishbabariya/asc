// Test: multiple variable declarations and arithmetic.

function main(): i32 {
  const a: i32 = 10;
  const b: i32 = 20;
  const c: i32 = a + b;
  const d: i32 = c * 2;
  return d - a;
}
