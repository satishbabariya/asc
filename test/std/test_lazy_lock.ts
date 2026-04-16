// RUN: %asc check %s
// Test: LazyLock thread-safe lazy initialization.
function compute_value(): i32 {
  return 42;
}

function main(): i32 {
  const lazy = LazyLock::new(compute_value);
  assert!(!lazy.is_initialized());
  const val = lazy.get();
  assert_eq!(*val, 42);
  assert!(lazy.is_initialized());
  const val2 = lazy.get();
  assert_eq!(*val2, 42);
  return 0;
}
