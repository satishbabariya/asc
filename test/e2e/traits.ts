// RUN: %asc check %s
// End-to-end test: traits and generics.

trait Display {
  fn to_string(ref<Self>): own<String>;
}

@copy
struct Point {
  x: f64,
  y: f64
}

impl Point {
  fn new(x: f64, y: f64): own<Point> {
    Point { x, y }
  }

  fn distance(ref<Point>, other: ref<Point>): f64 {
    return 0.0;
  }
}

function identity<T>(x: own<T>): own<T> {
  return x;
}

function main(): i32 {
  const p = Point::new(1.0, 2.0);
  return 0;
}
