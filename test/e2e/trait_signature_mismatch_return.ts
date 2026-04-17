// RUN: not %asc check %s 2>&1 | FileCheck %s
// CHECK: signature does not match
// Test: impl method with wrong return type is rejected.
// Registered Rem trait: fn rem(own<Self>, own<Self>): Self
// After Self -> N: fn rem(own<Self>, own<Self>): N
// This impl returns i32 instead of N — mismatch.

struct N { v: i32 }

impl Rem for N {
  fn rem(self: own<Self>, rhs: own<Self>): i32 {
    return self.v % rhs.v;
  }
}

function main(): i32 { return 0; }
