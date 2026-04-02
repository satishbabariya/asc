// Test: @copy struct assignment — copy semantics, no move.

@copy
struct Color { r: i32, g: i32, b: i32 }

function main(): i32 {
  let red = Color { r: 255, g: 0, b: 0 };
  let c = red;
  c = Color { r: 0, g: 255, b: 0 };
  return c.g;
}
