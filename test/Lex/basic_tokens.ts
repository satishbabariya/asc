// RUN: %asc check %s

// Basic token test — just verify the compiler doesn't crash.
// CHECK-NOT: internal error

function main(): i32 {
  const x: i32 = 42;
  return x;
}
