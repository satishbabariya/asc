// End-to-end test: simple function returning a constant.
// RUN: %asc build %s --emit mlir -o %t.mlir
// RUN: %asc check %s

function main(): i32 {
  const x: i32 = 42;
  return x;
}
