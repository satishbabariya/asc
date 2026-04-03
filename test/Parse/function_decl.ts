// RUN: %asc check %s 2>&1 | FileCheck %s || true

// CHECK-NOT: internal error

function add(a: i32, b: i32): i32 {
  return a + b;
}

function identity<T>(x: own<T>): own<T> {
  return x;
}
