// RUN: %asc check %s

// Basic type checking test.

function main(): i32 {
  const x: i32 = 42;
  const y: i32 = x + 1;
  return y;
}
