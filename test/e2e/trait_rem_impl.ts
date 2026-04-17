// RUN: %asc check %s
// Test: impl Rem for user-defined struct typechecks.

struct Wrap { v: i32 }

impl Rem for Wrap {
  fn rem(self: own<Self>, rhs: own<Self>): Wrap {
    return Wrap { v: self.v % rhs.v };
  }
}

function main(): i32 { return 0; }
