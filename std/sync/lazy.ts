// std/sync/lazy.ts — Lazy initialization primitive (RFC-0014)

/// A cell that computes its value on first access and caches it.
/// Not thread-safe — use `OnceLock<T>` for concurrent lazy initialization.
struct LazyCell<T> {
  value: Option<T>,
  init: Option<() => T>,
}

impl<T> LazyCell<T> {
  /// Create a new LazyCell with the given initialization function.
  /// The function will be called at most once, on the first access.
  fn new(init_fn: () => T): own<LazyCell<T>> {
    return LazyCell {
      value: Option::None,
      init: Option::Some(init_fn),
    };
  }

  /// Get a reference to the inner value, initializing it if necessary.
  fn get(ref<Self>): ref<T> {
    match self.value {
      Option::Some(ref v) => {
        return v;
      },
      Option::None => {
        // Take the init function out so it can only run once.
        let init_fn = self.init.take().unwrap();
        let val = init_fn();
        self.value = Option::Some(val);
        return self.value.as_ref().unwrap();
      },
    }
  }

  /// Returns true if the value has been initialized.
  fn is_initialized(ref<Self>): bool {
    return self.value.is_some();
  }

  /// Consume the LazyCell and return the inner value, initializing if needed.
  fn into_inner(own<Self>): T {
    match self.value {
      Option::Some(v) => {
        return v;
      },
      Option::None => {
        let init_fn = self.init.take().unwrap();
        return init_fn();
      },
    }
  }
}
