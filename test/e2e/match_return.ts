// RUN: %asc check %s
// Test: match with return in each arm.

enum Shape {
  Circle(i32),
  Square(i32),
}

function perimeter(s: Shape): i32 {
  match s {
    Shape::Circle(r) => { return r * 6; },
    Shape::Square(side) => { return side * 4; },
    _ => { return 0; },
  }
}

function main(): i32 {
  let c = Shape::Circle(7);
  return perimeter(c);
}
