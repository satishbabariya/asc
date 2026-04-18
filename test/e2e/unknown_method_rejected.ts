// RUN: %asc check %s > %t.out 2>&1; grep -q "no method" %t.out
// RUN: %asc check %s > %t.out 2>&1; grep -q "no_such_method" %t.out
// Test: calling a nonexistent method on a concrete type is rejected by Sema
// rather than silently accepted. Previously checkMethodCallExpr fell through
// returning nullptr without diagnostics, which let typos through asc check
// and defanged TDD workflows at the tooling layer.

struct Point { x: i32, y: i32 }

impl Point {
  fn new(x: i32, y: i32): own<Point> {
    return Point { x, y };
  }
}

function main(): i32 {
  let p = Point::new(1, 2);
  p.no_such_method();
  return 0;
}
