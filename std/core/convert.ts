// std/core/convert.ts — Conversion traits (RFC-0011)

/// Convert from one type to another, consuming the input.
trait From<T> {
  fn from(value: own<T>): own<Self>;
}

/// Reciprocal of From — automatically provided.
trait Into<T> {
  fn into(own<Self>): own<T>;
}

// Blanket impl: every T: From<U> implies U: Into<T>.
// This is handled by the compiler.

/// Fallible conversion.
trait TryFrom<T> {
  type Error;
  fn try_from(value: own<T>): Result<own<Self>, own<Error>>;
}

/// Reciprocal of TryFrom.
trait TryInto<T> {
  type Error;
  fn try_into(own<Self>): Result<own<T>, own<Error>>;
}

/// Borrow as a reference to another type.
trait AsRef<T> {
  fn as_ref(ref<Self>): ref<T>;
}

/// Borrow as a mutable reference to another type.
trait AsMut<T> {
  fn as_mut(refmut<Self>): refmut<T>;
}
