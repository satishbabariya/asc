// RUN: %asc check %s
// Test: struct literal used only locally should compile without errors.

struct Color { r: i32, g: i32, b: i32 }
function main(): i32 {
  let c = Color { r: 255, g: 0, b: 0 };
  return 0;
}
