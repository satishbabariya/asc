// std/mem/box.ts — Box<T>: heap-allocated owned pointer (RFC-0012)

/// Heap-allocated single-owner pointer. Layout: { ptr: *mut T }.
/// Send if T: Send, Sync if T: Sync.
struct Box<T> {
  ptr: *mut T,
}

impl<T> Box<T> {
  /// Allocates `value` on the heap and returns an owned Box.
  fn new(value: own<T>): own<Box<T>> {
    const ptr = malloc(size_of!<T>()) as *mut T;
    unsafe { ptr_write(ptr, value); }
    return Box { ptr: ptr };
  }

  /// Consumes the box, returning the owned inner value. Frees the allocation.
  fn into_inner(own<Self>): own<T> {
    const value = unsafe { ptr_read(self.ptr) };
    free(self.ptr);
    return value;
  }

  /// Returns a shared reference to the inner value.
  fn as_ref(ref<Self>): ref<T> {
    return unsafe { &*self.ptr };
  }

  /// Returns a mutable reference to the inner value.
  fn as_mut(refmut<Self>): refmut<T> {
    return unsafe { &mut *self.ptr };
  }

  /// Leaks the box, returning a 'static reference. The allocation is never freed.
  fn leak(own<Self>): ref<T> {
    const ptr = self.ptr;
    // Inhibit drop — do not free the allocation.
    forget(self);
    return unsafe { &*ptr };
  }
}

impl<T> Drop for Box<T> {
  fn drop(refmut<Self>): void {
    if self.ptr != null {
      unsafe { ptr_drop_in_place(self.ptr); }
      free(self.ptr);
    }
  }
}

impl<T> Deref for Box<T> {
  type Target = T;
  fn deref(ref<Self>): ref<T> {
    return self.as_ref();
  }
}

impl<T> DerefMut for Box<T> {
  fn deref_mut(refmut<Self>): refmut<T> {
    return self.as_mut();
  }
}

impl<T: Clone> Clone for Box<T> {
  fn clone(ref<Self>): own<Box<T>> {
    return Box::new(self.as_ref().clone());
  }
}

impl<T: Display> Display for Box<T> {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return self.as_ref().fmt(f);
  }
}
