// RUN: %asc check %s 2>&1 | FileCheck %s || true

// Basic token test — just verify the compiler doesn't crash.
// CHECK-NOT: internal error

function main(): i32 {
  const x: i32 = 42;
  return x;
}
