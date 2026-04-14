// RUN: %asc build %s --emit mlir > %t.out 2>&1
// RUN: ! grep -q "arith.addi" %t.out
// Test: constant arithmetic is folded by canonicalizer (no arith.addi remains).

function main(): i32 {
  let x: i32 = 2 + 3;
  return x;
}
