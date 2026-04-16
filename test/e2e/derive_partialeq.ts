// RUN: %asc check %s
// Test: @derive(PartialEq) generates callable eq method.

@derive(PartialEq)
struct Color { r: i32, g: i32, b: i32 }

function main(): i32 {
  let a = Color { r: 1, g: 2, b: 3 };
  let b = Color { r: 1, g: 2, b: 3 };
  assert!(a.eq(&b));
  return 0;
}
