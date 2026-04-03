// std/core/ops.ts — Operator traits (RFC-0011)

trait Add<Rhs = Self> {
  type Output;
  fn add(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait Sub<Rhs = Self> {
  type Output;
  fn sub(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait Mul<Rhs = Self> {
  type Output;
  fn mul(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait Div<Rhs = Self> {
  type Output;
  fn div(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait Rem<Rhs = Self> {
  type Output;
  fn rem(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait Neg {
  type Output;
  fn neg(own<Self>): own<Output>;
}

trait Not {
  type Output;
  fn not(own<Self>): own<Output>;
}

trait BitAnd<Rhs = Self> {
  type Output;
  fn bitand(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait BitOr<Rhs = Self> {
  type Output;
  fn bitor(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait BitXor<Rhs = Self> {
  type Output;
  fn bitxor(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait Shl<Rhs = Self> {
  type Output;
  fn shl(own<Self>, rhs: own<Rhs>): own<Output>;
}

trait Shr<Rhs = Self> {
  type Output;
  fn shr(own<Self>, rhs: own<Rhs>): own<Output>;
}

// Compound assignment operators.
trait AddAssign<Rhs = Self> {
  fn add_assign(refmut<Self>, rhs: own<Rhs>): void;
}

trait SubAssign<Rhs = Self> {
  fn sub_assign(refmut<Self>, rhs: own<Rhs>): void;
}

trait MulAssign<Rhs = Self> {
  fn mul_assign(refmut<Self>, rhs: own<Rhs>): void;
}

trait DivAssign<Rhs = Self> {
  fn div_assign(refmut<Self>, rhs: own<Rhs>): void;
}

trait RemAssign<Rhs = Self> {
  fn rem_assign(refmut<Self>, rhs: own<Rhs>): void;
}

/// Index operator: expr[index].
trait Index<Idx> {
  type Output;
  fn index(ref<Self>, idx: Idx): ref<Output>;
}

/// Mutable index operator: expr[index] = value.
trait IndexMut<Idx>: Index<Idx> {
  fn index_mut(refmut<Self>, idx: Idx): refmut<Output>;
}

/// Range types for .. and ..= operators.
@copy
struct Range<T> {
  start: T,
  end: T,
}

@copy
struct RangeInclusive<T> {
  start: T,
  end: T,
}

@copy
struct RangeFrom<T> {
  start: T,
}

@copy
struct RangeTo<T> {
  end: T,
}

@copy
struct RangeFull;
