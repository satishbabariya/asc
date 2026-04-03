// std/sync/mutex.ts — Mutex<T> with MutexGuard (RFC-0014)

/// Mutual exclusion lock. Send if T: Send, Sync if T: Send.
/// Uses atomic CAS for lock acquisition. Supports poison detection.
struct Mutex<T> {
  locked: i32,       // 0 = unlocked, 1 = locked (atomic)
  poisoned: bool,    // true if a thread panicked while holding the lock
  value: T,
}

impl<T> Mutex<T> {
  /// Creates a new unlocked mutex wrapping the given value.
  fn new(value: own<T>): own<Mutex<T>> {
    return Mutex { locked: 0, poisoned: false, value: value };
  }

  /// Acquires the lock, blocking until available.
  /// Returns Err if the mutex is poisoned (a previous holder panicked).
  fn lock(ref<Self>): Result<own<MutexGuard<T>>, PoisonError<T>> {
    const lock_ptr = unsafe { &self.locked as *const i32 as *mut i32 };
    // Spin-then-wait CAS loop.
    loop {
      @extern("__atomic_compare_exchange_n_i32")
      const result = atomic_compare_exchange(lock_ptr, 0, 1,
        Ordering::Acquire, Ordering::Relaxed);
      match result {
        Result::Ok(_) => { break; },
        Result::Err(_) => {
          // Wait on the futex until the lock word changes.
          @extern("memory.atomic.wait32")
          atomic_wait_i32(lock_ptr, 1, -1);
        },
      }
    }
    if self.poisoned {
      return Result::Err(PoisonError { mutex: self });
    }
    const value_ptr = unsafe { &self.value as *const T as *mut T };
    return Result::Ok(MutexGuard { mutex: self, value_ptr: value_ptr });
  }

  /// Tries to acquire the lock without blocking.
  fn try_lock(ref<Self>): Result<own<MutexGuard<T>>, TryLockError<T>> {
    const lock_ptr = unsafe { &self.locked as *const i32 as *mut i32 };
    @extern("__atomic_compare_exchange_n_i32")
    const result = atomic_compare_exchange(lock_ptr, 0, 1,
      Ordering::Acquire, Ordering::Relaxed);
    match result {
      Result::Ok(_) => {
        if self.poisoned {
          return Result::Err(TryLockError::Poisoned(PoisonError { mutex: self }));
        }
        const value_ptr = unsafe { &self.value as *const T as *mut T };
        return Result::Ok(MutexGuard { mutex: self, value_ptr: value_ptr });
      },
      Result::Err(_) => {
        return Result::Err(TryLockError::WouldBlock);
      },
    }
  }

  /// Consumes the mutex, returning the inner value.
  fn into_inner(own<Self>): Result<own<T>, PoisonError<T>> {
    if self.poisoned {
      return Result::Err(PoisonError { mutex: &self });
    }
    return Result::Ok(self.value);
  }

  /// Returns true if the mutex has been poisoned.
  fn is_poisoned(ref<Self>): bool {
    return self.poisoned;
  }
}

/// RAII guard returned by Mutex::lock(). Releases the lock on drop.
struct MutexGuard<T> {
  mutex: ref<Mutex<T>>,
  value_ptr: *mut T,
}

impl<T> MutexGuard<T> {
  fn deref(ref<Self>): ref<T> {
    return unsafe { &*self.value_ptr };
  }

  fn deref_mut(refmut<Self>): refmut<T> {
    return unsafe { &mut *self.value_ptr };
  }
}

impl<T> Drop for MutexGuard<T> {
  fn drop(refmut<Self>): void {
    // If the current thread is panicking, poison the mutex.
    if @intrinsic("is_panicking")() {
      const poisoned_ptr = unsafe {
        &self.mutex.poisoned as *const bool as *mut bool
      };
      unsafe { *poisoned_ptr = true; }
    }
    // Release the lock.
    const lock_ptr = unsafe { &self.mutex.locked as *const i32 as *mut i32 };
    @extern("__atomic_store_n_i32")
    atomic_store(lock_ptr, 0, Ordering::Release);
    // Wake one waiting thread.
    @extern("memory.atomic.notify")
    atomic_notify(lock_ptr, 1);
  }
}

struct PoisonError<T> {
  mutex: ref<Mutex<T>>,
}

enum TryLockError<T> {
  Poisoned(PoisonError<T>),
  WouldBlock,
}
