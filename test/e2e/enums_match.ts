// End-to-end test: enums and pattern matching.

enum Shape {
  Circle { radius: f64 },
  Rect { width: f64, height: f64 },
}

function area(shape: ref<Shape>): f64 {
  match shape {
    Shape::Circle { radius } => 3.14159 * radius * radius,
    Shape::Rect { width, height } => width * height,
  }
}

function main(): i32 {
  const c = Shape::Circle { radius: 5.0 };
  const r = Shape::Rect { width: 3.0, height: 4.0 };
  return 0;
}
