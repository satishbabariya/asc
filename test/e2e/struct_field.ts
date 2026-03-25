// Test struct creation and field access.

struct Point {
  x: f64,
  y: f64,
}

function distance(p: ref<Point>): f64 {
  return p.x * p.x + p.y * p.y;
}

function main(): i32 {
  const p = Point { x: 3.0, y: 4.0 };
  return 0;
}
