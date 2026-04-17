// RUN: %asc check %s
// Test: impl BitXor for user-defined struct typechecks.

struct Bits { v: u32 }

impl BitXor for Bits {
  fn bitxor(self: own<Self>, rhs: own<Self>): own<Bits> {
    return Bits { v: self.v ^ rhs.v };
  }
}

function main(): i32 { return 0; }
