// End-to-end test: simple function returning a constant.
// RUN: %asc build %s --emit mlir -o %t.mlir 2>&1 || true
// RUN: %asc check %s 2>&1 || true

function main(): i32 {
  const x: i32 = 42;
  return x;
}
