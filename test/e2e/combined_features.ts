// RUN: %asc check %s
// Test: multiple features combined.

@copy
struct Vec2 { x: f64, y: f64 }

function dot(a: Vec2, b: Vec2): f64 {
  return a.x * b.x + a.y * b.y;
}

function magnitude_sq(v: Vec2): f64 {
  return dot(v, v);
}

enum Shape {
  Circle { radius: f64 },
  Rect { width: f64, height: f64 },
}

function main(): i32 {
  const v = Vec2 { x: 3.0, y: 4.0 };
  const d = dot(v, v);
  return 0;
}
