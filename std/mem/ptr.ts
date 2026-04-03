// std/mem/ptr.ts — Raw pointer operations and NonNull<T> (RFC-0012)

/// Reads a value from `src` without moving it. The source is not dropped.
/// Safety: `src` must be valid, aligned, and properly initialized.
@extern("__asc_ptr_read")
unsafe fn ptr_read<T>(src: *const T): own<T> {
  const value_size = size_of!<T>();
  let result: T;
  memcpy(&mut result as *mut u8, src as *const u8, value_size);
  return result;
}

/// Writes a value to `dst` without reading or dropping the old value.
/// Safety: `dst` must be valid and aligned. Any previous value at `dst` is overwritten
/// without its destructor running.
@extern("__asc_ptr_write")
unsafe fn ptr_write<T>(dst: *mut T, value: own<T>): void {
  const value_size = size_of!<T>();
  memcpy(dst as *mut u8, &value as *const u8, value_size);
  forget(value);  // inhibit drop of the source — ownership transferred to dst
}

/// Copies `count` elements from `src` to `dst`. Regions may overlap.
/// Safety: both ranges must be valid and properly aligned.
@extern("__asc_ptr_copy")
unsafe fn ptr_copy<T>(src: *const T, dst: *mut T, count: usize): void {
  const byte_count = count * size_of!<T>();
  memmove(dst as *mut u8, src as *const u8, byte_count);
}

/// Copies `count` elements from `src` to `dst`. Regions must NOT overlap.
/// Safety: both ranges must be valid, properly aligned, and non-overlapping.
@extern("__asc_ptr_copy_nonoverlapping")
unsafe fn ptr_copy_nonoverlapping<T>(src: *const T, dst: *mut T, count: usize): void {
  const byte_count = count * size_of!<T>();
  memcpy(dst as *mut u8, src as *const u8, byte_count);
}

/// Drops the value pointed to by `ptr` without deallocating the memory.
/// Safety: `ptr` must be valid and the value must not be used after this call.
@extern("__asc_ptr_drop_in_place")
unsafe fn ptr_drop_in_place<T>(ptr: *mut T): void {
  // The compiler inserts the appropriate drop glue for T here.
  @intrinsic("drop_in_place")(ptr);
}

/// Writes `count` bytes of zeros to `dst`.
/// Safety: `dst` must be valid for `count * size_of::<T>()` bytes.
unsafe fn ptr_write_bytes<T>(dst: *mut T, val: u8, count: usize): void {
  const byte_count = count * size_of!<T>();
  memset(dst as *mut u8, val, byte_count);
}

/// Returns true if the pointer is null.
fn is_null<T>(ptr: *const T): bool {
  return ptr as usize == 0;
}

// ---------- NonNull<T> ----------

/// A non-null raw pointer. Guaranteed to never be null after construction.
/// This is the building block for smart pointer internals.
struct NonNull<T> {
  ptr: *mut T,
}

impl<T> NonNull<T> {
  /// Creates a NonNull from a raw pointer. Returns None if `ptr` is null.
  fn new(ptr: *mut T): Option<NonNull<T>> {
    if ptr as usize == 0 { return Option::None; }
    return Option::Some(NonNull { ptr: ptr });
  }

  /// Creates a NonNull from a raw pointer without checking for null.
  /// Safety: `ptr` must not be null.
  unsafe fn new_unchecked(ptr: *mut T): NonNull<T> {
    return NonNull { ptr: ptr };
  }

  /// Returns the raw pointer.
  fn as_ptr(ref<Self>): *mut T {
    return self.ptr;
  }

  /// Returns a shared reference to the pointee.
  /// Safety: the pointer must be valid and properly aligned.
  unsafe fn as_ref(ref<Self>): ref<T> {
    return &*self.ptr;
  }

  /// Returns a mutable reference to the pointee.
  /// Safety: the pointer must be valid, properly aligned, and no other reference must exist.
  unsafe fn as_mut(refmut<Self>): refmut<T> {
    return &mut *self.ptr;
  }

  /// Casts this NonNull<T> to a NonNull<U>.
  fn cast<U>(ref<Self>): NonNull<U> {
    return NonNull { ptr: self.ptr as *mut U };
  }
}

impl<T> Clone for NonNull<T> {
  fn clone(ref<Self>): own<NonNull<T>> {
    return NonNull { ptr: self.ptr };
  }
}

impl<T> PartialEq for NonNull<T> {
  fn eq(ref<Self>, other: ref<NonNull<T>>): bool {
    return self.ptr as usize == other.ptr as usize;
  }
}
