// RUN: %asc check %s
// Test: @derive(PartialEq) allows == comparison on struct.

@derive(PartialEq)
struct Color { r: i32, g: i32, b: i32 }

function main(): i32 {
  let a = Color { r: 1, g: 2, b: 3 };
  let b = Color { r: 1, g: 2, b: 3 };
  return 0;
}
