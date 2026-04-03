// End-to-end test: enums and pattern matching with tuple variants.

enum Shape {
  Circle(f64),
  Rect(f64, f64),
}

function classify(shape: Shape): i32 {
  return match shape {
    Shape::Circle(r) => 1,
    Shape::Rect(w, h) => 2,
    _ => 0,
  };
}

function main(): i32 {
  let c = Shape::Circle(5.0);
  let r = Shape::Rect(3.0, 4.0);
  let a: i32 = classify(c);
  let b: i32 = classify(r);
  return a + b;
}
