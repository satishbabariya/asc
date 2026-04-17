// std/mem/arc.ts — Arc<T>: atomic reference counted pointer (RFC-0012)

/// Atomic reference-counted heap pointer. Thread-safe (Send+Sync if T: Send+Sync).
/// Layout: { ptr: *mut ArcInner<T> } where ArcInner = { strong: AtomicUsize, weak: AtomicUsize, value: T }.
struct ArcInner<T> {
  strong: usize,  // atomic strong count
  weak: usize,    // atomic weak count
  value: T,
}

struct Arc<T> {
  ptr: *mut ArcInner<T>,
}

impl<T> Arc<T> {
  /// Allocates a new Arc with strong count = 1.
  fn new(value: own<T>): own<Arc<T>> {
    const ptr = malloc(size_of!<ArcInner<T>>()) as *mut ArcInner<T>;
    unsafe {
      ptr_write(&mut (*ptr).value, value);
      @extern("__atomic_store_n_usize")
      atomic_store(&(*ptr).strong, 1, Ordering::Release);
      @extern("__atomic_store_n_usize")
      atomic_store(&(*ptr).weak, 1, Ordering::Release);
    }
    return Arc { ptr: ptr };
  }

  /// Increments the strong count and returns a new owned Arc handle.
  fn clone(ref<Self>): own<Arc<T>> {
    @extern("__atomic_fetch_add_usize")
    const old = atomic_fetch_add(unsafe { &(*self.ptr).strong }, 1, Ordering::Relaxed);
    assert!(old > 0);
    return Arc { ptr: self.ptr };
  }

  /// Returns the current strong reference count.
  fn strong_count(ref<Self>): usize {
    @extern("__atomic_load_n_usize")
    return atomic_load(unsafe { &(*self.ptr).strong }, Ordering::Acquire);
  }

  /// Returns the current weak reference count (minus the implicit weak from strong holders).
  fn weak_count(ref<Self>): usize {
    @extern("__atomic_load_n_usize")
    const w = atomic_load(unsafe { &(*self.ptr).weak }, Ordering::Acquire);
    if w == 0 { return 0; }
    return w - 1;
  }

  /// Consumes the Arc and returns the inner value if this is the sole strong
  /// owner. Otherwise returns None and the Arc is dropped (decrementing the
  /// strong count). Unlike `try_unwrap`, the Arc is not returned on failure.
  fn into_inner(own<Self>): Option<own<T>> {
    match self.try_unwrap() {
      Result::Ok(v) => { return Option::Some(v); },
      Result::Err(_) => { return Option::None; },
    }
  }

  /// Attempts to unwrap the Arc. Returns Ok(T) if this is the sole owner, Err(Arc) otherwise.
  fn try_unwrap(own<Self>): Result<own<T>, own<Arc<T>>> {
    @extern("__atomic_compare_exchange_n_usize")
    const result = atomic_compare_exchange(
      unsafe { &(*self.ptr).strong }, 1, 0,
      Ordering::Acquire, Ordering::Relaxed
    );
    match result {
      Result::Ok(_) => {
        const value = unsafe { ptr_read(&(*self.ptr).value) };
        // Decrement weak count; free if zero.
        @extern("__atomic_fetch_sub_usize")
        const old_weak = atomic_fetch_sub(unsafe { &(*self.ptr).weak }, 1, Ordering::Release);
        if old_weak == 1 { free(self.ptr); }
        return Result::Ok(value);
      },
      Result::Err(_) => { return Result::Err(self); },
    }
  }

  /// Creates a Weak pointer from this Arc.
  fn downgrade(ref<Self>): own<Weak<T>> {
    @extern("__atomic_fetch_add_usize")
    atomic_fetch_add(unsafe { &(*self.ptr).weak }, 1, Ordering::Relaxed);
    return Weak { ptr: self.ptr };
  }

  /// Returns a shared reference to the inner value.
  fn as_ref(ref<Self>): ref<T> {
    return unsafe { &(*self.ptr).value };
  }

  /// Returns true if the two Arcs point to the same allocation.
  /// Does not compare values — only allocation identity.
  fn ptr_eq(a: ref<Arc<T>>, b: ref<Arc<T>>): bool {
    return a.ptr == b.ptr;
  }

  /// Returns a mutable reference to the inner value if this is the sole strong
  /// and weak reference. Returns None otherwise. Does not consume the Arc.
  fn get_mut(refmut<Self>): Option<refmut<T>> {
    @extern("__atomic_load_n_usize")
    const strong = atomic_load(unsafe { &(*self.ptr).strong }, Ordering::Acquire);
    if strong != 1 { return Option::None; }
    @extern("__atomic_load_n_usize")
    const weak = atomic_load(unsafe { &(*self.ptr).weak }, Ordering::Acquire);
    if weak != 1 { return Option::None; }
    return Option::Some(unsafe { &mut (*self.ptr).value });
  }
}

impl<T: Clone> Arc<T> {
  /// Clone-on-write: returns a mutable reference to the inner value. If the
  /// Arc is uniquely owned (strong == 1 AND weak == 1), returns a direct
  /// reference; otherwise clones the value into a new allocation and
  /// re-points this Arc at it before returning.
  fn make_mut(refmut<Self>): refmut<T> {
    @extern("__atomic_load_n_usize")
    const strong = atomic_load(unsafe { &(*self.ptr).strong }, Ordering::Acquire);
    @extern("__atomic_load_n_usize")
    const weak = atomic_load(unsafe { &(*self.ptr).weak }, Ordering::Acquire);
    if strong == 1 && weak == 1 {
      return unsafe { &mut (*self.ptr).value };
    }
    const cloned_value = self.as_ref().clone();
    @extern("__atomic_fetch_sub_usize")
    atomic_fetch_sub(unsafe { &(*self.ptr).strong }, 1, Ordering::Release);
    const new_ptr = malloc(size_of!<ArcInner<T>>()) as *mut ArcInner<T>;
    unsafe {
      ptr_write(&mut (*new_ptr).value, cloned_value);
      @extern("__atomic_store_n_usize")
      atomic_store(&(*new_ptr).strong, 1, Ordering::Release);
      @extern("__atomic_store_n_usize")
      atomic_store(&(*new_ptr).weak, 1, Ordering::Release);
    }
    self.ptr = new_ptr;
    return unsafe { &mut (*self.ptr).value };
  }
}

impl<T> Drop for Arc<T> {
  fn drop(refmut<Self>): void {
    @extern("__atomic_fetch_sub_usize")
    const old = atomic_fetch_sub(unsafe { &(*self.ptr).strong }, 1, Ordering::Release);
    if old == 1 {
      // Last strong reference — drop the value.
      @extern("__atomic_thread_fence")
      atomic_fence(Ordering::Acquire);
      unsafe { ptr_drop_in_place(&mut (*self.ptr).value); }
      // Decrement weak count (one implicit weak from strong holders).
      @extern("__atomic_fetch_sub_usize")
      const old_weak = atomic_fetch_sub(unsafe { &(*self.ptr).weak }, 1, Ordering::Release);
      if old_weak == 1 {
        free(self.ptr);
      }
    }
  }
}

impl<T> Deref for Arc<T> {
  type Target = T;
  fn deref(ref<Self>): ref<T> {
    return self.as_ref();
  }
}

/// Weak reference to an Arc-managed value. Does not keep the value alive.
struct Weak<T> {
  ptr: *mut ArcInner<T>,
}

impl<T> Weak<T> {
  /// Attempts to upgrade to a strong Arc. Returns None if all strong refs are gone.
  fn upgrade(ref<Self>): Option<own<Arc<T>>> {
    loop {
      @extern("__atomic_load_n_usize")
      const count = atomic_load(unsafe { &(*self.ptr).strong }, Ordering::Acquire);
      if count == 0 { return Option::None; }
      @extern("__atomic_compare_exchange_n_usize")
      const result = atomic_compare_exchange(
        unsafe { &(*self.ptr).strong }, count, count + 1,
        Ordering::AcqRel, Ordering::Relaxed
      );
      match result {
        Result::Ok(_) => { return Option::Some(Arc { ptr: self.ptr }); },
        Result::Err(_) => { /* retry */ },
      }
    }
  }

  fn strong_count(ref<Self>): usize {
    @extern("__atomic_load_n_usize")
    return atomic_load(unsafe { &(*self.ptr).strong }, Ordering::Acquire);
  }

  /// Returns true if the two Weaks point to the same allocation.
  fn ptr_eq(a: ref<Weak<T>>, b: ref<Weak<T>>): bool {
    return a.ptr == b.ptr;
  }
}

impl<T> Drop for Weak<T> {
  fn drop(refmut<Self>): void {
    @extern("__atomic_fetch_sub_usize")
    const old = atomic_fetch_sub(unsafe { &(*self.ptr).weak }, 1, Ordering::Release);
    if old == 1 {
      free(self.ptr);
    }
  }
}

impl<T> Clone for Weak<T> {
  fn clone(ref<Self>): own<Weak<T>> {
    @extern("__atomic_fetch_add_usize")
    atomic_fetch_add(unsafe { &(*self.ptr).weak }, 1, Ordering::Relaxed);
    return Weak { ptr: self.ptr };
  }
}
