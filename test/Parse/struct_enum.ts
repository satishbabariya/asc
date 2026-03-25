// RUN: %asc check %s 2>&1 | FileCheck %s || true

// CHECK-NOT: internal error

@copy
struct Point {
  x: f64,
  y: f64
}

enum Shape {
  Circle { radius: f64 },
  Rect { width: f64, height: f64 },
}

enum Option<T> {
  Some(own<T>),
  None,
}
