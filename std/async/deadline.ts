// std/async/deadline.ts — Deadline/timeout wrapper (RFC-0020)

/// Error returned when a deadline is exceeded.
enum DeadlineError {
  TimedOut,
}

impl Display for DeadlineError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      DeadlineError::TimedOut => f.write_str("operation timed out"),
    }
  }
}

/// Run an async function with a timeout. Returns DeadlineError::TimedOut
/// if the function does not complete within `timeout_ms` milliseconds.
async function deadline<T>(
  f: async () -> own<T>,
  timeout_ms: u64,
): Result<own<T>, DeadlineError> {
  let result_chan: own<Channel<Option<own<T>>>> = Channel::new(1);
  let sender = result_chan.sender();
  let receiver = result_chan.receiver();

  // Spawn the work.
  task::spawn(async || {
    let val = await f();
    sender.send(Option::Some(val));
  });

  // Race against the timer.
  let timer = sleep_async_ms(timeout_ms);
  match select! {
    val = receiver.recv() => val,
    _ = timer => Option::None,
  } {
    Option::Some(result) => Result::Ok(result),
    Option::None => Result::Err(DeadlineError::TimedOut),
  }
}

/// Run a synchronous function with a timeout on a separate thread.
function deadline_sync<T: Send>(
  f: () -> own<T>,
  timeout_ms: u64,
): Result<own<T>, DeadlineError> {
  let result_chan: own<Channel<Option<own<T>>>> = Channel::new(1);
  let sender = result_chan.sender();
  let receiver = result_chan.receiver();

  // Spawn work on a thread.
  thread::spawn(|| {
    let val = f();
    sender.send(Option::Some(val));
  });

  // Wait with timeout.
  match receiver.recv_timeout(timeout_ms) {
    Option::Some(val) => Result::Ok(val),
    Option::None => Result::Err(DeadlineError::TimedOut),
  }
}

/// Wrap a future with a deadline, returning a combined Result.
async function with_deadline<T, E>(
  f: async () -> Result<own<T>, own<E>>,
  timeout_ms: u64,
): Result<own<T>, DeadlineOrError<E>> {
  let result = await deadline(async || { await f() }, timeout_ms);
  match result {
    Result::Ok(inner) => {
      match inner {
        Result::Ok(v) => Result::Ok(v),
        Result::Err(e) => Result::Err(DeadlineOrError::Inner(e)),
      }
    },
    Result::Err(_) => Result::Err(DeadlineOrError::TimedOut),
  }
}

/// Combined error for deadline + inner error.
enum DeadlineOrError<E> {
  TimedOut,
  Inner(own<E>),
}

impl<E: Display> Display for DeadlineOrError<E> {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      DeadlineOrError::TimedOut => f.write_str("operation timed out"),
      DeadlineOrError::Inner(e) => e.fmt(f),
    }
  }
}

// --- Runtime-provided async sleep ---
@extern("env", "__asc_sleep_async_ms")
declare async function sleep_async_ms(ms: u64): void;
