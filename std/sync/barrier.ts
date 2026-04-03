// std/sync/barrier.ts — Barrier synchronization primitive (RFC-0014)

/// A barrier that blocks until `n` threads have all called `wait()`.
struct Barrier {
  num_threads: usize,
  count: i32,       // atomic: number of threads that have arrived
  generation: i32,  // atomic: incremented each time the barrier is released
}

impl Barrier {
  /// Creates a new barrier for `n` threads.
  fn new(n: usize): own<Barrier> {
    return Barrier { num_threads: n, count: 0, generation: 0 };
  }

  /// Blocks until all `n` threads have called `wait()`.
  /// Returns a BarrierWaitResult; exactly one thread per generation is the leader.
  fn wait(ref<Self>): BarrierWaitResult {
    const count_ptr = unsafe { &self.count as *const i32 as *mut i32 };
    const gen_ptr = unsafe { &self.generation as *const i32 as *mut i32 };

    // Record the current generation before incrementing count.
    @extern("__atomic_load_n_i32")
    const my_gen = atomic_load(gen_ptr, Ordering::Acquire);

    // Increment the arrival count.
    @extern("__atomic_fetch_add_i32")
    const arrived = atomic_fetch_add(count_ptr, 1, Ordering::AcqRel);
    const position = arrived + 1;

    if position as usize == self.num_threads {
      // We are the last thread to arrive — we are the leader.
      // Reset the count and advance the generation.
      @extern("__atomic_store_n_i32")
      atomic_store(count_ptr, 0, Ordering::Release);
      @extern("__atomic_fetch_add_i32")
      atomic_fetch_add(gen_ptr, 1, Ordering::Release);
      // Wake all waiting threads.
      @extern("memory.atomic.notify")
      atomic_notify(gen_ptr, 0xFFFFFFFF);
      return BarrierWaitResult { is_leader: true };
    }

    // Not the last thread — wait for the generation to advance.
    loop {
      @extern("memory.atomic.wait32")
      atomic_wait_i32(gen_ptr, my_gen, -1);
      @extern("__atomic_load_n_i32")
      const current_gen = atomic_load(gen_ptr, Ordering::Acquire);
      if current_gen != my_gen { break; }
    }

    return BarrierWaitResult { is_leader: false };
  }
}

/// Result returned by Barrier::wait().
struct BarrierWaitResult {
  is_leader: bool,
}

impl BarrierWaitResult {
  /// Returns true if this thread was the last to arrive (the "leader").
  fn is_leader(ref<Self>): bool {
    return self.is_leader;
  }
}
