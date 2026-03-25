// std/mem/rc.ts — Rc<T>: non-atomic reference counted pointer (RFC-0012)
// NOT Send, NOT Sync. Requires explicit import: import { Rc, Weak } from 'std/rc'

/// Non-atomic reference-counted pointer for single-threaded use.
/// Layout: { ptr: *mut RcInner<T> } where RcInner = { strong: usize, weak: usize, value: T }.
struct RcInner<T> {
  strong: usize,
  weak: usize,
  value: T,
}

struct Rc<T> {
  ptr: *mut RcInner<T>,
}

impl<T> Rc<T> {
  /// Allocates a new Rc with strong count = 1.
  fn new(value: own<T>): own<Rc<T>> {
    const ptr = malloc(size_of!<RcInner<T>>()) as *mut RcInner<T>;
    unsafe {
      ptr_write(&mut (*ptr).value, value);
      (*ptr).strong = 1;
      (*ptr).weak = 1;
    }
    return Rc { ptr: ptr };
  }

  /// Increments the strong count and returns a new owned Rc handle.
  fn clone(ref<Self>): own<Rc<T>> {
    unsafe {
      assert!((*self.ptr).strong > 0);
      (*self.ptr).strong = (*self.ptr).strong + 1;
    }
    return Rc { ptr: self.ptr };
  }

  /// Returns the current strong reference count.
  fn strong_count(ref<Self>): usize {
    return unsafe { (*self.ptr).strong };
  }

  /// Returns the current weak reference count (minus the implicit weak).
  fn weak_count(ref<Self>): usize {
    const w = unsafe { (*self.ptr).weak };
    if w == 0 { return 0; }
    return w - 1;
  }

  /// Attempts to unwrap. Returns Ok(T) if this is the sole strong owner.
  fn try_unwrap(own<Self>): Result<own<T>, own<Rc<T>>> {
    if unsafe { (*self.ptr).strong } != 1 {
      return Result::Err(self);
    }
    unsafe { (*self.ptr).strong = 0; }
    const value = unsafe { ptr_read(&(*self.ptr).value) };
    // Decrement implicit weak.
    unsafe { (*self.ptr).weak = (*self.ptr).weak - 1; }
    if unsafe { (*self.ptr).weak } == 0 {
      free(self.ptr);
    }
    return Result::Ok(value);
  }

  /// Creates a Weak pointer from this Rc.
  fn downgrade(ref<Self>): own<Weak<T>> {
    unsafe { (*self.ptr).weak = (*self.ptr).weak + 1; }
    return Weak { ptr: self.ptr };
  }

  /// Returns a shared reference to the inner value.
  fn as_ref(ref<Self>): ref<T> {
    return unsafe { &(*self.ptr).value };
  }
}

impl<T> Drop for Rc<T> {
  fn drop(refmut<Self>): void {
    unsafe {
      (*self.ptr).strong = (*self.ptr).strong - 1;
      if (*self.ptr).strong == 0 {
        // Last strong reference — drop the value.
        ptr_drop_in_place(&mut (*self.ptr).value);
        (*self.ptr).weak = (*self.ptr).weak - 1;
        if (*self.ptr).weak == 0 {
          free(self.ptr);
        }
      }
    }
  }
}

impl<T> Deref for Rc<T> {
  type Target = T;
  fn deref(ref<Self>): ref<T> {
    return self.as_ref();
  }
}

/// Weak reference to an Rc-managed value.
struct Weak<T> {
  ptr: *mut RcInner<T>,
}

impl<T> Weak<T> {
  /// Attempts to upgrade to a strong Rc. Returns None if all strong refs are gone.
  fn upgrade(ref<Self>): Option<own<Rc<T>>> {
    const count = unsafe { (*self.ptr).strong };
    if count == 0 { return Option::None; }
    unsafe { (*self.ptr).strong = count + 1; }
    return Option::Some(Rc { ptr: self.ptr });
  }

  fn strong_count(ref<Self>): usize {
    return unsafe { (*self.ptr).strong };
  }
}

impl<T> Drop for Weak<T> {
  fn drop(refmut<Self>): void {
    unsafe {
      (*self.ptr).weak = (*self.ptr).weak - 1;
      if (*self.ptr).weak == 0 {
        free(self.ptr);
      }
    }
  }
}

impl<T> Clone for Weak<T> {
  fn clone(ref<Self>): own<Weak<T>> {
    unsafe { (*self.ptr).weak = (*self.ptr).weak + 1; }
    return Weak { ptr: self.ptr };
  }
}
