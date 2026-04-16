// std/thread/channel.ts — Bounded and unbounded MPSC channels (RFC-0014, RFC-0007)

/// Creates a bounded channel with the given capacity.
/// Returns a (Sender, Receiver) pair.
fn bounded<T: Send>(capacity: usize): (own<Sender<T>>, own<Receiver<T>>) {
  assert!(capacity > 0);
  const header = malloc(size_of!<ChannelHeader<T>>()) as *mut ChannelHeader<T>;
  const buf = malloc(capacity * size_of!<T>()) as *mut T;
  unsafe {
    (*header).buffer = buf;
    (*header).capacity = capacity;
    (*header).head = 0;
    (*header).tail = 0;
    (*header).count = 0;
    (*header).sender_count = 1;
    (*header).receiver_alive = true;
    (*header).closed = false;
  }
  const sender = Sender { header: header };
  const receiver = Receiver { header: header };
  return (sender, receiver);
}

/// Creates an unbounded channel (dynamically growing).
fn unbounded<T: Send>(): (own<Sender<T>>, own<Receiver<T>>) {
  // Unbounded uses a large initial capacity and grows as needed.
  const initial_cap: usize = 256;
  const header = malloc(size_of!<ChannelHeader<T>>()) as *mut ChannelHeader<T>;
  const buf = malloc(initial_cap * size_of!<T>()) as *mut T;
  unsafe {
    (*header).buffer = buf;
    (*header).capacity = initial_cap;
    (*header).head = 0;
    (*header).tail = 0;
    (*header).count = 0;
    (*header).sender_count = 1;
    (*header).receiver_alive = true;
    (*header).closed = false;
  }
  const sender = Sender { header: header };
  const receiver = Receiver { header: header };
  return (sender, receiver);
}

/// Internal shared channel state.
struct ChannelHeader<T> {
  buffer: *mut T,
  capacity: usize,
  head: i32,            // atomic: read position
  tail: i32,            // atomic: write position
  count: i32,           // atomic: number of items in the buffer
  sender_count: i32,    // atomic: number of live Sender handles
  receiver_alive: bool, // atomic flag
  closed: bool,
}

/// Sending half of a channel. Clone to create multiple producers.
struct Sender<T> {
  header: *mut ChannelHeader<T>,
}

impl<T: Send> Sender<T> {
  /// Sends a value into the channel. Blocks if the channel is full.
  /// Returns Err(SendError) if the receiver has been dropped.
  fn send(ref<Self>, value: own<T>): Result<void, SendError<T>> {
    const h = self.header;
    // Check if receiver is alive.
    @extern("__atomic_load_n_i32")
    const alive = atomic_load(
      unsafe { &(*h).receiver_alive as *const bool as *const i32 }, Ordering::Acquire);
    if alive == 0 {
      return Result::Err(SendError { value: value });
    }

    // Wait until there is space.
    const count_ptr = unsafe { &(*h).count as *const i32 as *mut i32 };
    loop {
      @extern("__atomic_load_n_i32")
      const current_count = atomic_load(count_ptr, Ordering::Acquire);
      if current_count < unsafe { (*h).capacity } as i32 { break; }
      @extern("memory.atomic.wait32")
      atomic_wait_i32(count_ptr, current_count, -1);
    }

    // Write value to tail position.
    const tail_ptr = unsafe { &(*h).tail as *const i32 as *mut i32 };
    @extern("__atomic_fetch_add_i32")
    const pos = atomic_fetch_add(tail_ptr, 1, Ordering::AcqRel);
    const idx = (pos as usize) % unsafe { (*h).capacity };
    const elem_size = size_of!<T>();
    const slot = (unsafe { (*h).buffer } as usize + idx * elem_size) as *mut T;
    unsafe { ptr_write(slot, value); }

    // Increment count and notify receiver.
    @extern("__atomic_fetch_add_i32")
    atomic_fetch_add(count_ptr, 1, Ordering::Release);
    @extern("memory.atomic.notify")
    atomic_notify(count_ptr, 1);

    return Result::Ok(());
  }

  /// Tries to send without blocking. Returns Err if full or disconnected.
  fn try_send(ref<Self>, value: own<T>): Result<void, TrySendError<T>> {
    const h = self.header;
    @extern("__atomic_load_n_i32")
    const alive = atomic_load(
      unsafe { &(*h).receiver_alive as *const bool as *const i32 }, Ordering::Acquire);
    if alive == 0 {
      return Result::Err(TrySendError::Disconnected(value));
    }

    const count_ptr = unsafe { &(*h).count as *const i32 as *mut i32 };
    @extern("__atomic_load_n_i32")
    const current_count = atomic_load(count_ptr, Ordering::Acquire);
    if current_count >= unsafe { (*h).capacity } as i32 {
      return Result::Err(TrySendError::Full(value));
    }

    const tail_ptr = unsafe { &(*h).tail as *const i32 as *mut i32 };
    @extern("__atomic_fetch_add_i32")
    const pos = atomic_fetch_add(tail_ptr, 1, Ordering::AcqRel);
    const idx = (pos as usize) % unsafe { (*h).capacity };
    const elem_size = size_of!<T>();
    const slot = (unsafe { (*h).buffer } as usize + idx * elem_size) as *mut T;
    unsafe { ptr_write(slot, value); }

    @extern("__atomic_fetch_add_i32")
    atomic_fetch_add(count_ptr, 1, Ordering::Release);
    @extern("memory.atomic.notify")
    atomic_notify(count_ptr, 1);

    return Result::Ok(());
  }
}

impl<T> Clone for Sender<T> {
  fn clone(ref<Self>): own<Sender<T>> {
    @extern("__atomic_fetch_add_i32")
    atomic_fetch_add(
      unsafe { &(*self.header).sender_count as *const i32 as *mut i32 },
      1, Ordering::Relaxed);
    return Sender { header: self.header };
  }
}

impl<T> Drop for Sender<T> {
  fn drop(refmut<Self>): void {
    @extern("__atomic_fetch_sub_i32")
    const old = atomic_fetch_sub(
      unsafe { &(*self.header).sender_count as *const i32 as *mut i32 },
      1, Ordering::AcqRel);
    if old == 1 {
      // Last sender — mark channel closed and wake the receiver.
      const closed_ptr = unsafe { &(*self.header).closed as *const bool as *mut bool };
      unsafe { *closed_ptr = true; }
      const count_ptr = unsafe { &(*self.header).count as *const i32 as *mut i32 };
      @extern("memory.atomic.notify")
      atomic_notify(count_ptr, 0xFFFFFFFF);
    }
  }
}

/// Receiving half of a channel. NOT Clone — single consumer.
struct Receiver<T> {
  header: *mut ChannelHeader<T>,
}

impl<T: Send> Receiver<T> {
  /// Receives a value from the channel. Blocks until a value is available.
  /// Returns Err(RecvError) if all senders have been dropped and channel is empty.
  fn recv(ref<Self>): Result<own<T>, RecvError> {
    const h = self.header;
    const count_ptr = unsafe { &(*h).count as *const i32 as *mut i32 };

    loop {
      @extern("__atomic_load_n_i32")
      const current_count = atomic_load(count_ptr, Ordering::Acquire);
      if current_count > 0 {
        // Read value from head position.
        const head_ptr = unsafe { &(*h).head as *const i32 as *mut i32 };
        @extern("__atomic_fetch_add_i32")
        const pos = atomic_fetch_add(head_ptr, 1, Ordering::AcqRel);
        const idx = (pos as usize) % unsafe { (*h).capacity };
        const elem_size = size_of!<T>();
        const slot = (unsafe { (*h).buffer } as usize + idx * elem_size) as *const T;
        const value = unsafe { ptr_read(slot) };

        // Decrement count and notify senders.
        @extern("__atomic_fetch_sub_i32")
        atomic_fetch_sub(count_ptr, 1, Ordering::Release);
        @extern("memory.atomic.notify")
        atomic_notify(count_ptr, 1);

        return Result::Ok(value);
      }

      // Channel empty — check if closed.
      if unsafe { (*h).closed } { return Result::Err(RecvError {}); }

      // Wait for new items.
      @extern("memory.atomic.wait32")
      atomic_wait_i32(count_ptr, 0, -1);
    }
  }

  /// Tries to receive without blocking.
  fn try_recv(ref<Self>): Result<own<T>, TryRecvError> {
    const h = self.header;
    const count_ptr = unsafe { &(*h).count as *const i32 as *mut i32 };

    @extern("__atomic_load_n_i32")
    const current_count = atomic_load(count_ptr, Ordering::Acquire);
    if current_count > 0 {
      const head_ptr = unsafe { &(*h).head as *const i32 as *mut i32 };
      @extern("__atomic_fetch_add_i32")
      const pos = atomic_fetch_add(head_ptr, 1, Ordering::AcqRel);
      const idx = (pos as usize) % unsafe { (*h).capacity };
      const elem_size = size_of!<T>();
      const slot = (unsafe { (*h).buffer } as usize + idx * elem_size) as *const T;
      const value = unsafe { ptr_read(slot) };

      @extern("__atomic_fetch_sub_i32")
      atomic_fetch_sub(count_ptr, 1, Ordering::Release);
      @extern("memory.atomic.notify")
      atomic_notify(count_ptr, 1);

      return Result::Ok(value);
    }

    if unsafe { (*h).closed } { return Result::Err(TryRecvError::Disconnected); }
    return Result::Err(TryRecvError::Empty);
  }

  /// Receives a value from the channel with a timeout.
  /// Returns Err(RecvTimeoutError::Timeout) if the deadline elapses.
  /// Returns Err(RecvTimeoutError::Disconnected) if all senders are dropped.
  fn recv_timeout(ref<Self>, timeout_ms: u64): Result<own<T>, RecvTimeoutError> {
    const h = self.header;
    const count_ptr = unsafe { &(*h).count as *const i32 as *mut i32 };

    @extern("__asc_clock_monotonic")
    const start_ns = clock_monotonic();
    const deadline_ns = start_ns + timeout_ms * 1_000_000;

    loop {
      @extern("__atomic_load_n_i32")
      const current_count = atomic_load(count_ptr, Ordering::Acquire);
      if current_count > 0 {
        const head_ptr = unsafe { &(*h).head as *const i32 as *mut i32 };
        @extern("__atomic_fetch_add_i32")
        const pos = atomic_fetch_add(head_ptr, 1, Ordering::AcqRel);
        const idx = (pos as usize) % unsafe { (*h).capacity };
        const elem_size = size_of!<T>();
        const slot = (unsafe { (*h).buffer } as usize + idx * elem_size) as *const T;
        const value = unsafe { ptr_read(slot) };
        @extern("__atomic_fetch_sub_i32")
        atomic_fetch_sub(count_ptr, 1, Ordering::Release);
        @extern("memory.atomic.notify")
        atomic_notify(count_ptr, 1);
        return Result::Ok(value);
      }

      if unsafe { (*h).closed } {
        return Result::Err(RecvTimeoutError::Disconnected);
      }

      @extern("__asc_clock_monotonic")
      const now_ns = clock_monotonic();
      if now_ns >= deadline_ns {
        return Result::Err(RecvTimeoutError::Timeout);
      }

      const remaining_ns = deadline_ns - now_ns;
      const remaining_ms = (remaining_ns / 1_000_000) as i64;
      @extern("memory.atomic.wait32")
      atomic_wait_i32(count_ptr, 0, remaining_ms);
    }
  }

  /// Returns an iterator that yields values from the channel.
  /// The iterator blocks on each call to next() and terminates when
  /// the channel is closed and empty.
  fn iter(ref<Self>): own<RecvIter<T>> {
    return RecvIter { rx: self };
  }
}

impl<T> Drop for Receiver<T> {
  fn drop(refmut<Self>): void {
    // Mark receiver as dead so senders get errors.
    const alive_ptr = unsafe {
      &(*self.header).receiver_alive as *const bool as *mut bool
    };
    unsafe { *alive_ptr = false; }

    // Drop any remaining items in the buffer.
    const h = self.header;
    const count_ptr = unsafe { &(*h).count as *const i32 as *mut i32 };
    @extern("__atomic_load_n_i32")
    let remaining = atomic_load(count_ptr, Ordering::Acquire);
    while remaining > 0 {
      const head_ptr = unsafe { &(*h).head as *const i32 as *mut i32 };
      @extern("__atomic_fetch_add_i32")
      const pos = atomic_fetch_add(head_ptr, 1, Ordering::AcqRel);
      const idx = (pos as usize) % unsafe { (*h).capacity };
      const elem_size = size_of!<T>();
      const slot = (unsafe { (*h).buffer } as usize + idx * elem_size) as *mut T;
      unsafe { ptr_drop_in_place(slot); }
      remaining = remaining - 1;
    }

    // Free buffer and header.
    free(unsafe { (*h).buffer });
    free(h);
  }
}

// ---------- Error types ----------

struct SendError<T> {
  value: own<T>,
}

impl<T> SendError<T> {
  fn into_inner(own<Self>): own<T> { return self.value; }
}

enum TrySendError<T> {
  Full(own<T>),
  Disconnected(own<T>),
}

struct RecvError {}

enum TryRecvError {
  Empty,
  Disconnected,
}

enum RecvTimeoutError {
  Timeout,
  Disconnected,
}

struct RecvIter<T> {
  rx: ref<Receiver<T>>,
}

impl<T: Send> Iterator for RecvIter<T> {
  type Item = T;
  fn next(refmut<Self>): Option<own<T>> {
    match self.rx.recv() {
      Result::Ok(v) => { return Option::Some(v); },
      Result::Err(_) => { return Option::None; },
    }
  }
}
