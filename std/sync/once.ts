// std/sync/once.ts — Once: one-time initialization primitive (RFC-0014)

/// States for Once.
const ONCE_INCOMPLETE: i32 = 0;
const ONCE_RUNNING: i32 = 1;
const ONCE_COMPLETE: i32 = 2;
const ONCE_POISONED: i32 = 3;

/// A synchronization primitive for one-time initialization.
/// `call_once` guarantees the closure runs exactly once, even across multiple threads.
struct Once {
  state: i32,  // atomic
}

impl Once {
  /// Creates a new incomplete Once.
  fn new(): Once {
    return Once { state: ONCE_INCOMPLETE };
  }

  /// Calls `f` if this is the first invocation. Subsequent calls are no-ops.
  /// If `f` panics, the Once is poisoned and future calls will also panic.
  fn call_once(ref<Self>, f: FnOnce()): void {
    const state_ptr = unsafe { &self.state as *const i32 as *mut i32 };
    @extern("__atomic_load_n_i32")
    const current = atomic_load(state_ptr, Ordering::Acquire);
    if current == ONCE_COMPLETE { return; }
    if current == ONCE_POISONED { panic!("Once instance has been poisoned"); }

    // Try to claim the RUNNING state.
    @extern("__atomic_compare_exchange_n_i32")
    const result = atomic_compare_exchange(state_ptr, ONCE_INCOMPLETE, ONCE_RUNNING,
      Ordering::AcqRel, Ordering::Acquire);
    match result {
      Result::Ok(_) => {
        // We are the chosen thread — run the closure.
        // If f panics, the panic handler will mark us as poisoned.
        // TODO: integrate with panic/unwind (RFC-0009).
        f();
        @extern("__atomic_store_n_i32")
        atomic_store(state_ptr, ONCE_COMPLETE, Ordering::Release);
        // Wake all waiters.
        @extern("memory.atomic.notify")
        atomic_notify(state_ptr, 0xFFFFFFFF);
      },
      Result::Err(actual) => {
        // Another thread is running or already completed.
        if actual == ONCE_RUNNING {
          // Wait for completion.
          loop {
            @extern("memory.atomic.wait32")
            atomic_wait_i32(state_ptr, ONCE_RUNNING, -1);
            @extern("__atomic_load_n_i32")
            const s = atomic_load(state_ptr, Ordering::Acquire);
            if s == ONCE_COMPLETE { return; }
            if s == ONCE_POISONED { panic!("Once instance has been poisoned"); }
          }
        }
        if actual == ONCE_COMPLETE { return; }
        if actual == ONCE_POISONED { panic!("Once instance has been poisoned"); }
      },
    }
  }

  /// Calls `f` even if a previous call panicked.
  fn call_once_force(ref<Self>, f: FnOnce(ref<OnceState>)): void {
    const state_ptr = unsafe { &self.state as *const i32 as *mut i32 };
    @extern("__atomic_load_n_i32")
    const current = atomic_load(state_ptr, Ordering::Acquire);
    if current == ONCE_COMPLETE { return; }

    // Try to claim from INCOMPLETE or POISONED.
    let expected = current;
    if expected == ONCE_RUNNING { expected = ONCE_INCOMPLETE; }
    @extern("__atomic_compare_exchange_n_i32")
    const result = atomic_compare_exchange(state_ptr, expected, ONCE_RUNNING,
      Ordering::AcqRel, Ordering::Acquire);
    match result {
      Result::Ok(_) => {
        const once_state = OnceState { poisoned: expected == ONCE_POISONED };
        f(&once_state);
        @extern("__atomic_store_n_i32")
        atomic_store(state_ptr, ONCE_COMPLETE, Ordering::Release);
        @extern("memory.atomic.notify")
        atomic_notify(state_ptr, 0xFFFFFFFF);
      },
      Result::Err(_) => {
        // Another thread is handling it — wait.
        loop {
          @extern("memory.atomic.wait32")
          atomic_wait_i32(state_ptr, ONCE_RUNNING, -1);
          @extern("__atomic_load_n_i32")
          const s = atomic_load(state_ptr, Ordering::Acquire);
          if s == ONCE_COMPLETE { return; }
        }
      },
    }
  }

  /// Returns true if `call_once` has completed successfully.
  fn is_completed(ref<Self>): bool {
    const state_ptr = unsafe { &self.state as *const i32 };
    @extern("__atomic_load_n_i32")
    return atomic_load(state_ptr, Ordering::Acquire) == ONCE_COMPLETE;
  }
}

/// State passed to `call_once_force`.
struct OnceState {
  poisoned: bool,
}

impl OnceState {
  /// Returns true if the Once was previously poisoned.
  fn is_poisoned(ref<Self>): bool {
    return self.poisoned;
  }
}
