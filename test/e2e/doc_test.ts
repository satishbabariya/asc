// RUN: %asc check %s
/// Adds two numbers together.
function add(a: i32, b: i32): i32 {
  return a + b;
}

/// A point in 2D space.
struct Point {
  x: f64,
  y: f64,
}

/// Computes the distance from origin.
function distance(p: ref<Point>): f64 {
  return p.x * p.x + p.y * p.y;
}

function main(): i32 {
  return 0;
}
