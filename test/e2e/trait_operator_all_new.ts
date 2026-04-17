// RUN: %asc check %s
// Test: all 8 newly-registered operator traits (RFC-0011) compile together.

struct N { v: u32 }

impl Rem for N {
  fn rem(self: own<Self>, rhs: own<Self>): own<N> {
    return N { v: self.v % rhs.v };
  }
}

impl BitAnd for N {
  fn bitand(self: own<Self>, rhs: own<Self>): own<N> {
    return N { v: self.v & rhs.v };
  }
}

impl BitOr for N {
  fn bitor(self: own<Self>, rhs: own<Self>): own<N> {
    return N { v: self.v | rhs.v };
  }
}

impl BitXor for N {
  fn bitxor(self: own<Self>, rhs: own<Self>): own<N> {
    return N { v: self.v ^ rhs.v };
  }
}

impl Shl for N {
  fn shl(self: own<Self>, rhs: own<Self>): own<N> {
    return N { v: self.v << rhs.v };
  }
}

impl Shr for N {
  fn shr(self: own<Self>, rhs: own<Self>): own<N> {
    return N { v: self.v >> rhs.v };
  }
}

impl AddAssign for N {
  fn add_assign(self: refmut<Self>, rhs: own<Self>): void {
    self.v = self.v + rhs.v;
  }
}

impl SubAssign for N {
  fn sub_assign(self: refmut<Self>, rhs: own<Self>): void {
    self.v = self.v - rhs.v;
  }
}

function main(): i32 { return 0; }
