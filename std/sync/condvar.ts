// std/sync/condvar.ts — Condvar: condition variable (RFC-0014)

/// Condition variable for thread synchronization.
/// Used with Mutex to wait for a condition to become true.
struct Condvar {
  futex: i32,  // atomic: incremented on each notify to detect spurious wakeups
}

impl<T> Condvar {
  /// Creates a new condition variable.
  fn new(): own<Condvar> {
    return Condvar { futex: 0 };
  }

  /// Blocks the current thread until notified.
  /// Releases the mutex guard, waits, then re-acquires and returns the guard.
  fn wait<T>(ref<Self>, guard: own<MutexGuard<T>>): Result<own<MutexGuard<T>>, PoisonError<T>> {
    const futex_ptr = unsafe { &self.futex as *const i32 as *mut i32 };
    @extern("__atomic_load_n_i32")
    const seq = atomic_load(futex_ptr, Ordering::Acquire);

    // Get the mutex reference before dropping the guard.
    const mutex = guard.mutex;
    // Drop the guard to release the lock.
    drop(guard);

    // Wait until the sequence number changes (i.e., a notify happened).
    @extern("memory.atomic.wait32")
    atomic_wait_i32(futex_ptr, seq, -1);

    // Re-acquire the mutex.
    return mutex.lock();
  }

  /// Blocks until `condition` returns false.
  /// Automatically re-checks the condition on each wakeup to handle spurious wakes.
  fn wait_while<T>(ref<Self>, guard: own<MutexGuard<T>>, condition: (ref<T>) -> bool): Result<own<MutexGuard<T>>, PoisonError<T>> {
    let g = guard;
    while condition(g.deref()) {
      match self.wait(g) {
        Result::Ok(new_guard) => { g = new_guard; },
        Result::Err(e) => { return Result::Err(e); },
      }
    }
    return Result::Ok(g);
  }

  /// Wakes one thread waiting on this condvar.
  fn notify_one(ref<Self>): void {
    const futex_ptr = unsafe { &self.futex as *const i32 as *mut i32 };
    @extern("__atomic_fetch_add_i32")
    atomic_fetch_add(futex_ptr, 1, Ordering::Release);
    @extern("memory.atomic.notify")
    atomic_notify(futex_ptr, 1);
  }

  /// Wakes all threads waiting on this condvar.
  fn notify_all(ref<Self>): void {
    const futex_ptr = unsafe { &self.futex as *const i32 as *mut i32 };
    @extern("__atomic_fetch_add_i32")
    atomic_fetch_add(futex_ptr, 1, Ordering::Release);
    @extern("memory.atomic.notify")
    atomic_notify(futex_ptr, 0xFFFFFFFF);
  }
}
