// RUN: not %asc check %s 2>&1 | grep -q "signature does not match"
// Test: impl method using ref<Self> where trait declares own<Self> is rejected.

struct N { v: i32 }

impl Rem for N {
  fn rem(self: ref<Self>, rhs: ref<Self>): own<N> {
    return N { v: self.v % rhs.v };
  }
}

function main(): i32 { return 0; }
