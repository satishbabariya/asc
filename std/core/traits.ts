// std/core/traits.ts — Core lifecycle traits (RFC-0011)
//
// These traits are auto-imported into every module via the prelude.

/// Runs the destructor when an owned value goes out of scope.
/// Copy and Drop are mutually exclusive — @copy types cannot impl Drop.
trait Drop {
  fn drop(refmut<Self>): void;
}

// Copy is a marker trait — handled by @copy attribute on structs.
// All numeric primitives (i8–i64, u8–u64, f32, f64, bool, char, usize) are Copy.

/// Explicit deep copy. Distinguished from Copy: Clone is always explicit
/// (you call .clone()); Copy is implicit.
trait Clone {
  fn clone(ref<Self>): own<Self>;
}

// Send and Sync are marker traits — handled by @send/@sync attributes.
// Send: safe to move across thread boundary (task.spawn requires this).
// Sync: safe to share references across threads (ref<T> is Send iff T: Sync).

/// Zero-value construction.
trait Default {
  fn default(): own<Self>;
}

/// Deref coercion: Box<T> → T, Vec<T> → [T], String → str.
trait Deref {
  type Target;
  fn deref(ref<Self>): ref<Target>;
}

trait DerefMut: Deref {
  fn deref_mut(refmut<Self>): refmut<Target>;
}
