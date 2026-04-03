// std/mem/arena.ts — Arena<T>: bump allocator (RFC-0012)

/// Bump allocator for same-type bulk allocation. All allocations freed together on drop.
/// References issued by the arena are scoped to the arena's lifetime (enforced by borrow checker).
struct Arena<T> {
  ptr: *mut T,    // backing buffer
  len: usize,     // number of live objects
  cap: usize,     // capacity in number of T
}

impl<T> Arena<T> {
  /// Creates a new empty arena with default capacity.
  fn new(): own<Arena<T>> {
    return Arena::with_capacity(64);
  }

  /// Creates a new arena pre-allocated for `capacity` elements.
  fn with_capacity(capacity: usize): own<Arena<T>> {
    const elem_size = size_of!<T>();
    const ptr = malloc(capacity * elem_size) as *mut T;
    return Arena { ptr: ptr, len: 0, cap: capacity };
  }

  /// Allocates a value in the arena, returning a shared reference.
  fn alloc(refmut<Self>, value: own<T>): ref<T> {
    if self.len == self.cap { self.grow(); }
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + self.len * elem_size) as *mut T;
    unsafe { ptr_write(slot, value); }
    self.len = self.len + 1;
    return unsafe { &*slot };
  }

  /// Allocates a value in the arena, returning a mutable reference.
  fn alloc_mut(refmut<Self>, value: own<T>): refmut<T> {
    if self.len == self.cap { self.grow(); }
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + self.len * elem_size) as *mut T;
    unsafe { ptr_write(slot, value); }
    self.len = self.len + 1;
    return unsafe { &mut *slot };
  }

  /// Returns the number of allocated objects.
  fn len(ref<Self>): usize { return self.len; }

  /// Returns true if the arena has no allocations.
  fn is_empty(ref<Self>): bool { return self.len == 0; }

  /// Returns the total capacity before the next reallocation.
  fn capacity(ref<Self>): usize { return self.cap; }

  // Internal: grow the backing buffer by doubling capacity.
  fn grow(refmut<Self>): void {
    let new_cap = self.cap * 2;
    if new_cap < 16 { new_cap = 16; }
    const elem_size = size_of!<T>();
    const new_ptr = malloc(new_cap * elem_size) as *mut T;
    if self.ptr != null {
      memcpy(new_ptr as *mut u8, self.ptr as *const u8, self.len * elem_size);
      free(self.ptr);
    }
    self.ptr = new_ptr;
    self.cap = new_cap;
  }
}

impl<T> Drop for Arena<T> {
  fn drop(refmut<Self>): void {
    // Drop all allocated objects in reverse order.
    let i = self.len;
    while i > 0 {
      i = i - 1;
      const elem_size = size_of!<T>();
      const slot = (self.ptr as usize + i * elem_size) as *mut T;
      unsafe { ptr_drop_in_place(slot); }
    }
    // Free the entire backing buffer in one call.
    if self.ptr != null { free(self.ptr); }
  }
}
