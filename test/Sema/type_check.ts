// RUN: %asc check %s 2>&1 | FileCheck %s || true

// Basic type checking test.

function main(): i32 {
  const x: i32 = 42;
  const y: i32 = x + 1;
  return y;
}
