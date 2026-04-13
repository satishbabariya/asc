// RUN: %asc check %s
// Test: @copy struct used multiple times -- no linearity error.
// @copy types are not wrapped in !own.val<T> so linearity does not apply.

@copy
struct Point { x: f64, y: f64 }

function main(): f64 {
  const p = Point { x: 1.0, y: 2.0 };
  const q = p;
  return p.x + q.y;
}
