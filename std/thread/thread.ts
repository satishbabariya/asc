// std/thread/thread.ts — Thread spawning and management (RFC-0014, RFC-0007)

/// Opaque thread identifier. @copy.
struct ThreadId {
  id: u32,
}

impl ThreadId {
  fn eq(ref<Self>, other: ref<ThreadId>): bool {
    return self.id == other.id;
  }
}

impl Display for ThreadId {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_u32(self.id);
  }
}

/// Handle to a spawned thread. Owns the join state.
struct JoinHandle<R> {
  thread_id: ThreadId,
  result_ptr: *mut Option<Result<own<R>, PanicInfo>>,  // shared memory for result
  done: *mut i32,  // atomic flag: 0 = running, 1 = finished
}

/// Spawns a new thread running closure `f`. Returns a JoinHandle.
/// F must be Send (no non-Send captures) and FnOnce.
fn spawn<F, R>(f: own<F>): own<JoinHandle<R>>
  where F: FnOnce() -> R + Send, R: Send {
  // Allocate shared state for the result.
  const result_ptr = malloc(size_of!<Option<Result<own<R>, PanicInfo>>>())
    as *mut Option<Result<own<R>, PanicInfo>>;
  unsafe { ptr_write(result_ptr, Option::None); }
  const done_ptr = malloc(size_of!<i32>()) as *mut i32;
  unsafe { ptr_write(done_ptr, 0); }

  // Wrap the closure and shared state into a thread entry.
  @extern("wasi_thread_spawn")
  const tid = wasi_thread_spawn(|| {
    const result = f();
    unsafe { ptr_write(result_ptr, Option::Some(Result::Ok(result))); }
    @extern("__atomic_store_n_i32")
    atomic_store(done_ptr, 1, Ordering::Release);
    @extern("memory.atomic.notify")
    atomic_notify(done_ptr, 0xFFFFFFFF);
  });

  return JoinHandle {
    thread_id: ThreadId { id: tid },
    result_ptr: result_ptr,
    done: done_ptr,
  };
}

impl<R> JoinHandle<R> {
  /// Blocks until the thread finishes and returns its result.
  fn join(own<Self>): Result<own<R>, PanicInfo> {
    // Wait until the thread signals completion.
    loop {
      @extern("__atomic_load_n_i32")
      const finished = atomic_load(self.done, Ordering::Acquire);
      if finished != 0 { break; }
      @extern("memory.atomic.wait32")
      atomic_wait_i32(self.done, 0, -1);
    }
    const result = unsafe { ptr_read(self.result_ptr) };
    free(self.result_ptr);
    free(self.done);
    match result {
      Option::Some(r) => { return r; },
      Option::None => { panic!("thread result missing"); },
    }
  }

  /// Returns the thread's ID.
  fn id(ref<Self>): ThreadId {
    return self.thread_id;
  }

  /// Returns true if the thread has finished executing.
  fn is_finished(ref<Self>): bool {
    @extern("__atomic_load_n_i32")
    const finished = atomic_load(self.done, Ordering::Acquire);
    return finished != 0;
  }
}

impl<R> Drop for JoinHandle<R> {
  fn drop(refmut<Self>): void {
    // If the handle is dropped without join, detach the thread.
    // The shared memory will leak — this is intentional for detached threads.
    // A production implementation would use a ref-counted block.
  }
}

/// Returns the current thread's ID.
fn current_id(): ThreadId {
  @extern("__asc_thread_id")
  const tid = get_current_thread_id();
  return ThreadId { id: tid };
}

/// Suspends the current thread for the given duration.
fn sleep(duration: Duration): void {
  const nanos = duration.as_nanos();
  @extern("__asc_thread_sleep")
  thread_sleep_nanos(nanos);
}

/// Thread information for the current thread.
struct Thread {
  id: ThreadId,
  name: Option<own<String>>,
}

impl Thread {
  /// Returns the thread's ID.
  fn id(ref<Self>): ThreadId {
    return self.id;
  }

  /// Returns the thread's name, if set.
  fn name(ref<Self>): Option<ref<str>> {
    match &self.name {
      Option::Some(s) => { return Option::Some(s.as_str()); },
      Option::None => { return Option::None; },
    }
  }
}

/// Panic information from a panicked thread.
struct PanicInfo {
  message: own<String>,
}

impl PanicInfo {
  fn message(ref<Self>): ref<str> {
    return self.message.as_str();
  }
}

impl Display for PanicInfo {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str(self.message.as_str());
  }
}
