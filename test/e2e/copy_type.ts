// Test @copy semantics: Point is copied, not moved.

@copy
struct Point { x: f64, y: f64 }

function main(): f64 {
  const p = Point { x: 1.0, y: 2.0 };
  const q = p;
  return p.x + q.y;
}
