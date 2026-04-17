// RUN: %asc check %s
// Test: impl Shr for user-defined struct typechecks.

struct Bits { v: u32 }

impl Shr for Bits {
  fn shr(self: own<Self>, rhs: own<Self>): Bits {
    return Bits { v: self.v >> rhs.v };
  }
}

function main(): i32 { return 0; }
