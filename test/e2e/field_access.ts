// Test: struct field access end-to-end.
// Expected: compiles to valid MLIR with GEP + load for field access.

struct Pair {
  a: i32,
  b: i32,
}

function main(): i32 {
  const p = Pair { a: 10, b: 20 };
  return p.a + p.b;
}
