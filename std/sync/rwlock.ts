// std/sync/rwlock.ts — RwLock<T> with read/write guards (RFC-0014)

/// Reader-writer lock. Multiple readers OR one writer at any time.
/// Uses atomic operations for state management.
/// State encoding: 0 = unlocked, positive = N readers, -1 = writer holds lock.
struct RwLock<T> {
  state: i32,        // atomic: reader count or -1 for writer
  writer_wake: i32,  // atomic: futex address for writer wakeup
  poisoned: bool,
  value: T,
}

impl<T> RwLock<T> {
  /// Creates a new unlocked RwLock.
  fn new(value: own<T>): own<RwLock<T>> {
    return RwLock { state: 0, writer_wake: 0, poisoned: false, value: value };
  }

  /// Acquires a shared read lock, blocking until available.
  fn read(ref<Self>): Result<own<RwLockReadGuard<T>>, PoisonError<T>> {
    const state_ptr = unsafe { &self.state as *const i32 as *mut i32 };
    loop {
      @extern("__atomic_load_n_i32")
      const current = atomic_load(state_ptr, Ordering::Acquire);
      if current >= 0 {
        @extern("__atomic_compare_exchange_n_i32")
        const result = atomic_compare_exchange(state_ptr, current, current + 1,
          Ordering::AcqRel, Ordering::Relaxed);
        match result {
          Result::Ok(_) => { break; },
          Result::Err(_) => { /* retry */ },
        }
      } else {
        // Writer holds lock — wait.
        @extern("memory.atomic.wait32")
        atomic_wait_i32(state_ptr, current, -1);
      }
    }
    if self.poisoned {
      return Result::Err(PoisonError {});
    }
    return Result::Ok(RwLockReadGuard { lock: self, value_ptr: &self.value });
  }

  /// Acquires an exclusive write lock, blocking until available.
  fn write(ref<Self>): Result<own<RwLockWriteGuard<T>>, PoisonError<T>> {
    const state_ptr = unsafe { &self.state as *const i32 as *mut i32 };
    loop {
      @extern("__atomic_compare_exchange_n_i32")
      const result = atomic_compare_exchange(state_ptr, 0, -1,
        Ordering::AcqRel, Ordering::Relaxed);
      match result {
        Result::Ok(_) => { break; },
        Result::Err(_) => {
          @extern("__atomic_load_n_i32")
          const current = atomic_load(state_ptr, Ordering::Relaxed);
          @extern("memory.atomic.wait32")
          atomic_wait_i32(state_ptr, current, -1);
        },
      }
    }
    if self.poisoned {
      return Result::Err(PoisonError {});
    }
    const value_ptr = unsafe { &self.value as *const T as *mut T };
    return Result::Ok(RwLockWriteGuard { lock: self, value_ptr: value_ptr });
  }

  /// Tries to acquire a shared read lock without blocking.
  fn try_read(ref<Self>): Result<own<RwLockReadGuard<T>>, TryLockError<T>> {
    const state_ptr = unsafe { &self.state as *const i32 as *mut i32 };
    @extern("__atomic_load_n_i32")
    const current = atomic_load(state_ptr, Ordering::Acquire);
    if current < 0 { return Result::Err(TryLockError::WouldBlock); }
    @extern("__atomic_compare_exchange_n_i32")
    const result = atomic_compare_exchange(state_ptr, current, current + 1,
      Ordering::AcqRel, Ordering::Relaxed);
    match result {
      Result::Ok(_) => {
        return Result::Ok(RwLockReadGuard { lock: self, value_ptr: &self.value });
      },
      Result::Err(_) => { return Result::Err(TryLockError::WouldBlock); },
    }
  }

  /// Tries to acquire an exclusive write lock without blocking.
  fn try_write(ref<Self>): Result<own<RwLockWriteGuard<T>>, TryLockError<T>> {
    const state_ptr = unsafe { &self.state as *const i32 as *mut i32 };
    @extern("__atomic_compare_exchange_n_i32")
    const result = atomic_compare_exchange(state_ptr, 0, -1,
      Ordering::AcqRel, Ordering::Relaxed);
    match result {
      Result::Ok(_) => {
        const value_ptr = unsafe { &self.value as *const T as *mut T };
        return Result::Ok(RwLockWriteGuard { lock: self, value_ptr: value_ptr });
      },
      Result::Err(_) => { return Result::Err(TryLockError::WouldBlock); },
    }
  }

  /// Consumes the lock, returning the inner value.
  fn into_inner(own<Self>): Result<own<T>, PoisonError<T>> {
    if self.poisoned { return Result::Err(PoisonError {}); }
    return Result::Ok(self.value);
  }
}

/// RAII guard for a shared read lock.
struct RwLockReadGuard<T> {
  lock: ref<RwLock<T>>,
  value_ptr: ref<T>,
}

impl<T> Deref for RwLockReadGuard<T> {
  type Target = T;
  fn deref(ref<Self>): ref<T> {
    return self.value_ptr;
  }
}

impl<T> Drop for RwLockReadGuard<T> {
  fn drop(refmut<Self>): void {
    const state_ptr = unsafe { &self.lock.state as *const i32 as *mut i32 };
    @extern("__atomic_fetch_sub_i32")
    const old = atomic_fetch_sub(state_ptr, 1, Ordering::Release);
    if old == 1 {
      // Last reader — wake a waiting writer.
      @extern("memory.atomic.notify")
      atomic_notify(state_ptr, 1);
    }
  }
}

/// RAII guard for an exclusive write lock.
struct RwLockWriteGuard<T> {
  lock: ref<RwLock<T>>,
  value_ptr: *mut T,
}

impl<T> Deref for RwLockWriteGuard<T> {
  type Target = T;
  fn deref(ref<Self>): ref<T> {
    return unsafe { &*self.value_ptr };
  }
}

impl<T> DerefMut for RwLockWriteGuard<T> {
  fn deref_mut(refmut<Self>): refmut<T> {
    return unsafe { &mut *self.value_ptr };
  }
}

impl<T> Drop for RwLockWriteGuard<T> {
  fn drop(refmut<Self>): void {
    if @intrinsic("is_panicking")() {
      const poisoned_ptr = unsafe {
        &self.lock.poisoned as *const bool as *mut bool
      };
      unsafe { *poisoned_ptr = true; }
    }
    const state_ptr = unsafe { &self.lock.state as *const i32 as *mut i32 };
    @extern("__atomic_store_n_i32")
    atomic_store(state_ptr, 0, Ordering::Release);
    // Wake all waiters (both readers and writers).
    @extern("memory.atomic.notify")
    atomic_notify(state_ptr, 0xFFFFFFFF);
  }
}

struct PoisonError<T> {}

enum TryLockError<T> {
  Poisoned(PoisonError<T>),
  WouldBlock,
}
