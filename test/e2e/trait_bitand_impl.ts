// RUN: %asc check %s
// Test: impl BitAnd for user-defined struct typechecks.

struct Bits { v: u32 }

impl BitAnd for Bits {
  fn bitand(self: own<Self>, rhs: own<Self>): own<Bits> {
    return Bits { v: self.v & rhs.v };
  }
}

function main(): i32 { return 0; }
