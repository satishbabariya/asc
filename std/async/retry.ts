// std/async/retry.ts — Retry with exponential backoff (RFC-0020)

/// Configuration for retry behavior.
struct RetryOptions {
  /// Maximum number of attempts (including the first).
  max_attempts: u32,
  /// Initial delay in milliseconds before the first retry.
  initial_delay_ms: u64,
  /// Maximum delay in milliseconds between retries.
  max_delay_ms: u64,
  /// Multiplier for exponential backoff (fixed-point: 2000 = 2.0x).
  backoff_multiplier: u64,
  /// Whether to add jitter to the delay.
  jitter: bool,
}

impl RetryOptions {
  fn default(): own<RetryOptions> {
    return RetryOptions {
      max_attempts: 3,
      initial_delay_ms: 100,
      max_delay_ms: 30000,
      backoff_multiplier: 2000, // 2.0x
      jitter: true,
    };
  }

  fn with_max_attempts(own<Self>, n: u32): own<RetryOptions> {
    self.max_attempts = n;
    return self;
  }

  fn with_initial_delay(own<Self>, ms: u64): own<RetryOptions> {
    self.initial_delay_ms = ms;
    return self;
  }

  fn with_max_delay(own<Self>, ms: u64): own<RetryOptions> {
    self.max_delay_ms = ms;
    return self;
  }

  fn with_backoff(own<Self>, multiplier: u64): own<RetryOptions> {
    self.backoff_multiplier = multiplier;
    return self;
  }

  fn with_jitter(own<Self>, enabled: bool): own<RetryOptions> {
    self.jitter = enabled;
    return self;
  }
}

/// Error returned when all retry attempts are exhausted.
enum RetryError<E> {
  Exhausted(own<E>),
}

impl<E: Display> Display for RetryError<E> {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      RetryError::Exhausted(e) => {
        f.write_str("retry exhausted: ")?;
        e.fmt(f)
      },
    }
  }
}

/// Retry a fallible function with exponential backoff.
/// The function is called up to `options.max_attempts` times.
function retry<T, E>(
  f: () -> Result<own<T>, own<E>>,
  options: ref<RetryOptions>,
): Result<own<T>, RetryError<E>> {
  let attempt: u32 = 0;
  let delay = options.initial_delay_ms;
  let last_err: Option<own<E>> = Option::None;

  while attempt < options.max_attempts {
    let result = f();
    match result {
      Result::Ok(v) => { return Result::Ok(v); },
      Result::Err(e) => {
        last_err = Option::Some(e);
        attempt = attempt + 1;
        if attempt < options.max_attempts {
          let actual_delay = delay;
          if options.jitter {
            // Simple jitter: delay * [0.5, 1.5)
            // Approximated without floating point.
            let jitter_range = delay;
            let jitter_offset = delay / 2;
            let rand_val = simple_hash(attempt as u64 * 7 + delay) % jitter_range;
            actual_delay = jitter_offset + rand_val;
          }
          sleep_ms(actual_delay);
          // Exponential backoff.
          delay = (delay * options.backoff_multiplier) / 1000;
          if delay > options.max_delay_ms { delay = options.max_delay_ms; }
        }
      },
    }
  }

  return Result::Err(RetryError::Exhausted(last_err.unwrap()));
}

/// Retry with a custom predicate that decides whether to retry on each error.
/// If should_retry returns false, stop immediately.
function retry_if<T, E>(
  f: () -> Result<own<T>, own<E>>,
  options: ref<RetryOptions>,
  should_retry: (ref<E>) -> bool,
): Result<own<T>, RetryError<E>> {
  let attempt: u32 = 0;
  let delay = options.initial_delay_ms;
  let last_err: Option<own<E>> = Option::None;

  while attempt < options.max_attempts {
    let result = f();
    match result {
      Result::Ok(v) => { return Result::Ok(v); },
      Result::Err(e) => {
        if !should_retry(&e) {
          return Result::Err(RetryError::Exhausted(e));
        }
        last_err = Option::Some(e);
        attempt = attempt + 1;
        if attempt < options.max_attempts {
          let actual_delay = delay;
          if options.jitter {
            let jitter_range = delay;
            let jitter_offset = delay / 2;
            let rand_val = simple_hash(attempt as u64 * 7 + delay) % jitter_range;
            actual_delay = jitter_offset + rand_val;
          }
          sleep_ms(actual_delay);
          delay = (delay * options.backoff_multiplier) / 1000;
          if delay > options.max_delay_ms { delay = options.max_delay_ms; }
        }
      },
    }
  }

  return Result::Err(RetryError::Exhausted(last_err.unwrap()));
}

/// Retry an async fallible function with exponential backoff.
async function retry_async<T, E>(
  f: async () -> Result<own<T>, own<E>>,
  options: ref<RetryOptions>,
): Result<own<T>, RetryError<E>> {
  let attempt: u32 = 0;
  let delay = options.initial_delay_ms;
  let last_err: Option<own<E>> = Option::None;

  while attempt < options.max_attempts {
    let result = await f();
    match result {
      Result::Ok(v) => { return Result::Ok(v); },
      Result::Err(e) => {
        last_err = Option::Some(e);
        attempt = attempt + 1;
        if attempt < options.max_attempts {
          let actual_delay = delay;
          if options.jitter {
            let jitter_range = delay;
            let jitter_offset = delay / 2;
            let rand_val = simple_hash(attempt as u64 * 7 + delay) % jitter_range;
            actual_delay = jitter_offset + rand_val;
          }
          await sleep_async_ms(actual_delay);
          delay = (delay * options.backoff_multiplier) / 1000;
          if delay > options.max_delay_ms { delay = options.max_delay_ms; }
        }
      },
    }
  }

  return Result::Err(RetryError::Exhausted(last_err.unwrap()));
}

/// Simple deterministic hash for jitter (not cryptographic).
function simple_hash(x: u64): u64 {
  let v = x;
  v = v ^ (v >> 33);
  v = v *% 0xff51afd7ed558ccd;
  v = v ^ (v >> 33);
  v = v *% 0xc4ceb9fe1a85ec53;
  v = v ^ (v >> 33);
  return v;
}

/// Synchronous sleep (provided by runtime).
@extern("env", "__asc_sleep_ms")
declare function sleep_ms(ms: u64): void;

/// Async sleep (provided by runtime).
@extern("env", "__asc_sleep_async_ms")
declare async function sleep_async_ms(ms: u64): void;
