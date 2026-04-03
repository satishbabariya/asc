// Test: enum with tuple variants and match.

enum Shape {
  Circle(i32),
  Rectangle(i32, i32),
}

function perimeter(s: Shape): i32 {
  match s {
    Shape::Circle(r) => { return 2 * 3 * r; },
    Shape::Rectangle(w, h) => { return 2 * (w + h); },
    _ => { return 0; },
  }
}

function main(): i32 {
  let c = Shape::Circle(7);
  let r = Shape::Rectangle(10, 5);
  return perimeter(c) + perimeter(r);
}
