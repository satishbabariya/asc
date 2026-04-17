// RUN: %asc check %s
// Test: impl SubAssign for user-defined struct typechecks.

struct Counter { v: i32 }

impl SubAssign for Counter {
  fn sub_assign(self: refmut<Self>, rhs: own<Self>): void {
    self.v = self.v - rhs.v;
  }
}

function main(): i32 { return 0; }
