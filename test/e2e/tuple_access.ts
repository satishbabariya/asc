// RUN: %asc check %s
// Test: tuple creation and field access via t.0 syntax.

function main(): i32 {
  let t = (10, 20, 30);
  let a: i32 = t.0;
  let b: i32 = t.1;
  let c: i32 = t.2;
  return a + b + c;
}
