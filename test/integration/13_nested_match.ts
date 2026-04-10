// RUN: %asc check %s
// test 13: multi-field enum payloads
enum Shape {
  Circle(f64),
  Rect(f64, f64),
}

function area(s: Shape): f64 {
  match s {
    Shape::Circle(r) => 3.14159 * r * r,
    Shape::Rect(w, h) => w * h,
  }
}

function main(): i32 {
  let c = Shape::Circle(10.0);
  let r = Shape::Rect(5.0, 8.0);
  let total: f64 = area(c) + area(r);
  if total > 354.0 {
    return 1;
  }
  return 0;
}
