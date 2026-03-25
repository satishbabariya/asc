// std/core/cmp.ts — Comparison traits (RFC-0011)

/// Result of a comparison between two values.
@copy
enum Ordering {
  Less,
  Equal,
  Greater,
}

/// Partial equality comparison. Implemented for all types that support ==.
trait PartialEq {
  fn eq(ref<Self>, other: ref<Self>): bool;
  fn ne(ref<Self>, other: ref<Self>): bool {
    return !self.eq(other);
  }
}

/// Full equality: reflexive, symmetric, transitive.
/// f32/f64 implement PartialEq but NOT Eq (NaN ≠ NaN).
trait Eq: PartialEq {}

/// Partial ordering. Returns None for incomparable values (e.g., NaN).
trait PartialOrd: PartialEq {
  fn partial_cmp(ref<Self>, other: ref<Self>): Option<Ordering>;

  fn lt(ref<Self>, other: ref<Self>): bool {
    match self.partial_cmp(other) {
      Option::Some(Ordering::Less) => true,
      _ => false,
    }
  }
  fn le(ref<Self>, other: ref<Self>): bool {
    match self.partial_cmp(other) {
      Option::Some(Ordering::Less) => true,
      Option::Some(Ordering::Equal) => true,
      _ => false,
    }
  }
  fn gt(ref<Self>, other: ref<Self>): bool {
    match self.partial_cmp(other) {
      Option::Some(Ordering::Greater) => true,
      _ => false,
    }
  }
  fn ge(ref<Self>, other: ref<Self>): bool {
    match self.partial_cmp(other) {
      Option::Some(Ordering::Greater) => true,
      Option::Some(Ordering::Equal) => true,
      _ => false,
    }
  }
}

/// Total ordering. All values are comparable.
trait Ord: Eq + PartialOrd {
  fn cmp(ref<Self>, other: ref<Self>): Ordering;

  fn max(own<Self>, other: own<Self>): own<Self> {
    match self.cmp(&other) {
      Ordering::Less => other,
      _ => self,
    }
  }
  fn min(own<Self>, other: own<Self>): own<Self> {
    match self.cmp(&other) {
      Ordering::Greater => other,
      _ => self,
    }
  }
  fn clamp(own<Self>, min: own<Self>, max: own<Self>): own<Self> {
    if self.cmp(&min) == Ordering::Less { return min; }
    if self.cmp(&max) == Ordering::Greater { return max; }
    return self;
  }
}

/// Reverse ordering wrapper for descending sorts.
@copy
struct Reverse<T: Ord> {
  value: T,
}

impl<T: Ord> Ord for Reverse<T> {
  fn cmp(ref<Self>, other: ref<Self>): Ordering {
    match other.value.cmp(&self.value) {
      Ordering::Less => Ordering::Less,
      Ordering::Equal => Ordering::Equal,
      Ordering::Greater => Ordering::Greater,
    }
  }
}
