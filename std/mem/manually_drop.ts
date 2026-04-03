// std/mem/manually_drop.ts — ManuallyDrop<T> and MaybeUninit<T> (RFC-0012)

/// Wrapper that inhibits automatic Drop for its inner value.
/// The compiler will NOT insert a drop call for ManuallyDrop<T>.
/// The user must explicitly call `drop()` or `into_inner()` to clean up.
struct ManuallyDrop<T> {
  value: T,
}

impl<T> ManuallyDrop<T> {
  /// Wraps a value, suppressing its automatic destructor.
  fn new(value: own<T>): own<ManuallyDrop<T>> {
    return ManuallyDrop { value: value };
  }

  /// Extracts the inner value, consuming the wrapper.
  /// Safety: must not be called after `drop()` has been called.
  unsafe fn into_inner(own<Self>): own<T> {
    return self.value;
  }

  /// Manually drops the contained value.
  /// Safety: must not be called more than once, and `into_inner` must not
  /// be called after this.
  unsafe fn drop(refmut<Self>): void {
    ptr_drop_in_place(&mut self.value);
  }

  /// Returns a shared reference to the inner value.
  fn deref(ref<Self>): ref<T> {
    return &self.value;
  }

  /// Returns a mutable reference to the inner value.
  fn deref_mut(refmut<Self>): refmut<T> {
    return &mut self.value;
  }
}

// No Drop impl — that is the whole point.

// ---------- MaybeUninit<T> ----------

/// A wrapper for potentially uninitialized memory. Used in FFI, Vec internals,
/// and any scenario requiring deferred initialization.
/// Reading an uninitialized MaybeUninit is undefined behavior.
struct MaybeUninit<T> {
  storage: T,  // may contain uninitialized bytes
}

impl<T> MaybeUninit<T> {
  /// Creates an uninitialized MaybeUninit. The storage is NOT zeroed.
  fn uninit(): MaybeUninit<T> {
    // The compiler emits an alloca without a store — no initialization.
    return @intrinsic("uninit") as MaybeUninit<T>;
  }

  /// Creates a MaybeUninit initialized with the given value.
  fn new(value: own<T>): MaybeUninit<T> {
    return MaybeUninit { storage: value };
  }

  /// Writes a value into the MaybeUninit, returning a mutable reference.
  /// If the MaybeUninit was already initialized, the old value is NOT dropped.
  fn write(refmut<Self>, value: own<T>): refmut<T> {
    unsafe { ptr_write(&mut self.storage, value); }
    return &mut self.storage;
  }

  /// Extracts the value, assuming it has been initialized.
  /// Safety: calling this on uninitialized memory is undefined behavior.
  unsafe fn assume_init(own<Self>): own<T> {
    return self.storage;
  }

  /// Returns a shared reference, assuming initialized.
  /// Safety: calling this on uninitialized memory is undefined behavior.
  unsafe fn assume_init_ref(ref<Self>): ref<T> {
    return &self.storage;
  }

  /// Returns a mutable reference, assuming initialized.
  /// Safety: calling this on uninitialized memory is undefined behavior.
  unsafe fn assume_init_mut(refmut<Self>): refmut<T> {
    return &mut self.storage;
  }

  /// Returns a raw pointer to the storage.
  fn as_ptr(ref<Self>): *const T {
    return &self.storage as *const T;
  }

  /// Returns a raw mutable pointer to the storage.
  fn as_mut_ptr(refmut<Self>): *mut T {
    return &mut self.storage as *mut T;
  }
}

// No Drop impl — MaybeUninit does not drop its contents automatically.
// The user is responsible for either calling assume_init() or manually
// dropping the inner value via ptr_drop_in_place.
