// RUN: %asc check %s
// Test: impl BitOr for user-defined struct typechecks.

struct Bits { v: u32 }

impl BitOr for Bits {
  fn bitor(self: own<Self>, rhs: own<Self>): own<Bits> {
    return Bits { v: self.v | rhs.v };
  }
}

function main(): i32 { return 0; }
