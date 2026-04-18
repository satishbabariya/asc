// std/async/deadline.ts — Deadline/timeout wrapper (RFC-0020 §3)
//
// ## Cooperative cancellation model
//
// `deadline` does NOT preemptively kill the worker thread when the timeout
// expires — that would leak locks, channels, and heap allocations the worker
// owns. Instead, the model is **cooperative**:
//
//   1. The caller spawns `work` on a worker thread via `thread::spawn`.
//   2. The caller blocks on `recv_timeout` against a single-slot result
//      channel. Whichever wins — work finishes or timer fires — determines
//      the return value.
//   3. On `TimedOut`, the worker is **not** stopped. It continues until
//      `work` returns on its own, at which point its result is dropped by
//      the channel receiver (whose tail is freed in its Drop impl).
//   4. Long-running tasks that need true cancellation should poll a shared
//      `AtomicBool` flag themselves — this function gives them the signal
//      (by returning `TimedOut`) but does not enforce termination.
//
// The `Cancelled` variant of `DeadlineError` is reserved for higher-level
// wrappers that plumb an explicit cancel token through the worker. The
// sync `deadline` / `deadline_at` entry points below never construct it
// themselves — they only ever return `TimedOut`.

/// Error returned by `deadline` / `deadline_at`.
enum DeadlineError {
  /// The timeout elapsed before `work` completed.
  TimedOut,
  /// A cancel token was tripped before `work` completed. Reserved for
  /// higher-level APIs that plumb an explicit cancellation signal.
  Cancelled,
}

impl Display for DeadlineError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      DeadlineError::TimedOut => f.write_str("operation timed out"),
      DeadlineError::Cancelled => f.write_str("operation cancelled"),
    }
  }
}

/// Run `work` on a spawned thread. Return its result if it completes within
/// `timeout_ms` milliseconds, otherwise `Err(DeadlineError::TimedOut)`.
///
/// On timeout, the worker thread is **not** killed — see the module-level
/// docs on cooperative cancellation. The result channel outlives this call
/// and drops the late value if/when the worker eventually finishes.
function deadline<T: Send>(
  timeout_ms: u64,
  work: () -> own<T>,
): Result<own<T>, DeadlineError> {
  const (sender, receiver) = bounded::<T>(1);

  // Hand work off to a worker thread. The worker sends its result into the
  // single-slot channel; if we've already returned on timeout, that send
  // will still succeed (capacity=1, no reader), and the value will be
  // dropped when `receiver` is dropped on scope exit.
  thread::spawn(move || {
    const val = work();
    // Ignore SendError — if the receiver was already dropped it means we
    // timed out and nobody cares about this value anymore.
    let _ = sender.send(val);
  });

  match receiver.recv_timeout(timeout_ms) {
    Result::Ok(v) => { return Result::Ok(v); },
    Result::Err(RecvTimeoutError::Timeout) => {
      return Result::Err(DeadlineError::TimedOut);
    },
    Result::Err(RecvTimeoutError::Disconnected) => {
      // Worker panicked before sending. Surface as TimedOut — the caller
      // sees "no value arrived" either way, and we have no PanicInfo here.
      return Result::Err(DeadlineError::TimedOut);
    },
  }
}

/// Run `work` with an **absolute** deadline given as a monotonic-clock
/// millisecond timestamp. Equivalent to:
///
///   deadline(max(0, deadline_ms - now_ms), work)
///
/// ...but computed once up front so retries against the same absolute
/// deadline share a budget.
function deadline_at<T: Send>(
  deadline_ms: u64,
  work: () -> own<T>,
): Result<own<T>, DeadlineError> {
  const now_ms = current_time_ms();
  let remaining: u64 = 0;
  if deadline_ms > now_ms {
    remaining = deadline_ms - now_ms;
  }
  // remaining == 0 is a legitimate "instant timeout" — we still spawn the
  // worker but will almost certainly time out immediately. That mirrors
  // Rust's `tokio::time::timeout_at(past)` behaviour.
  return deadline::<T>(remaining, work);
}

/// Async variant: run an async function with a timeout.
/// Retained for compatibility with higher-level code that uses the
/// compile-time `async` syntax. The sync `deadline` above is the
/// canonical form today.
async function deadline_async<T>(
  timeout_ms: u64,
  f: async () -> own<T>,
): Result<own<T>, DeadlineError> {
  let result_chan: own<Channel<Option<own<T>>>> = Channel::new(1);
  let sender = result_chan.sender();
  let receiver = result_chan.receiver();

  task::spawn(async || {
    let val = await f();
    sender.send(Option::Some(val));
  });

  let timer = sleep_async_ms(timeout_ms);
  match select! {
    val = receiver.recv() => val,
    _ = timer => Option::None,
  } {
    Option::Some(result) => Result::Ok(result),
    Option::None => Result::Err(DeadlineError::TimedOut),
  }
}

/// Wrap a fallible function with a deadline, returning a combined Result.
function with_deadline<T: Send, E: Send>(
  timeout_ms: u64,
  f: () -> Result<own<T>, own<E>>,
): Result<own<T>, DeadlineOrError<E>> {
  match deadline::<Result<own<T>, own<E>>>(timeout_ms, f) {
    Result::Ok(inner) => match inner {
      Result::Ok(v) => Result::Ok(v),
      Result::Err(e) => Result::Err(DeadlineOrError::Inner(e)),
    },
    Result::Err(DeadlineError::TimedOut) => Result::Err(DeadlineOrError::TimedOut),
    Result::Err(DeadlineError::Cancelled) => Result::Err(DeadlineOrError::Cancelled),
  }
}

/// Combined error for deadline + inner error.
enum DeadlineOrError<E> {
  TimedOut,
  Cancelled,
  Inner(own<E>),
}

impl<E: Display> Display for DeadlineOrError<E> {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      DeadlineOrError::TimedOut => f.write_str("operation timed out"),
      DeadlineOrError::Cancelled => f.write_str("operation cancelled"),
      DeadlineOrError::Inner(e) => e.fmt(f),
    }
  }
}

// --- Runtime-provided timer functions ---

@extern("env", "__asc_sleep_async_ms")
declare async function sleep_async_ms(ms: u64): void;

@extern("env", "__asc_current_time_ms")
declare function current_time_ms(): u64;
