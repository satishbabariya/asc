// Test: enum variant creation and pattern matching.

enum Color { Red, Green, Blue }

function color_code(c: Color): i32 {
  match c {
    Color::Red => 1,
    Color::Green => 2,
    Color::Blue => 3,
    _ => 0,
  }
}

function main(): i32 {
  return 0;
}
