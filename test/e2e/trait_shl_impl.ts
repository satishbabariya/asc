// RUN: %asc check %s
// Test: impl Shl for user-defined struct typechecks.

struct Bits { v: u32 }

impl Shl for Bits {
  fn shl(self: own<Self>, rhs: own<Self>): Bits {
    return Bits { v: self.v << rhs.v };
  }
}

function main(): i32 { return 0; }
