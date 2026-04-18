// std/async/throttle.ts — Rate limiting and throttled function wrappers (RFC-0020)

import { AtomicU64, AtomicU32, AtomicBool } from '../sync/atomic';
import { Mutex } from '../sync/mutex';

/// A token-bucket rate limiter. Limits how often an operation can proceed by
/// issuing a fixed number of permits per time interval.
struct RateLimiter {
  interval_ms: u64,
  last_refill_ms: own<AtomicU64>,
  permits: own<AtomicU32>,
  max_permits: u32,
}

impl RateLimiter {
  /// Create a new rate limiter that allows `max_permits` operations per
  /// `interval_ms` milliseconds.
  fn new(max_permits: u32, interval_ms: u64): own<RateLimiter> {
    return RateLimiter {
      interval_ms: interval_ms,
      last_refill_ms: AtomicU64::new(current_time_ms()),
      permits: AtomicU32::new(max_permits),
      max_permits: max_permits,
    };
  }

  /// Try to refill permits if enough time has elapsed since the last refill.
  fn try_refill(ref<Self>): void {
    let now = current_time_ms();
    let last = self.last_refill_ms.load();
    if now - last >= self.interval_ms {
      if self.last_refill_ms.compare_exchange(last, now) {
        self.permits.store(self.max_permits);
      }
    }
  }

  /// Acquire a permit, blocking conceptually until one is available.
  /// Returns true when a permit is acquired. In a single-threaded or
  /// cooperative environment this will spin-wait on the timer.
  fn acquire(ref<Self>): bool {
    loop {
      self.try_refill();
      let current = self.permits.load();
      if current > 0 {
        if self.permits.compare_exchange(current, current - 1) {
          return true;
        }
        // CAS failed — another thread took the permit, retry.
        continue;
      }
      // No permits available; spin until refill.
      // In a real runtime this would yield or sleep.
    }
  }

  /// Try to acquire a permit without blocking. Returns true if a permit was
  /// consumed, false if none are available right now.
  fn try_acquire(ref<Self>): bool {
    self.try_refill();
    let current = self.permits.load();
    if current > 0 {
      if self.permits.compare_exchange(current, current - 1) {
        return true;
      }
    }
    return false;
  }

  /// Return the number of permits currently available.
  fn available(ref<Self>): u32 {
    self.try_refill();
    return self.permits.load();
  }

  /// Reset the limiter to full capacity.
  fn reset(ref<Self>): void {
    self.permits.store(self.max_permits);
    self.last_refill_ms.store(current_time_ms());
  }

  /// Try to consume n tokens at once. Returns false if not enough.
  fn try_acquire_n(ref<Self>, n: u32): bool {
    self.try_refill();
    let current = self.permits.load();
    if current >= n {
      if self.permits.compare_exchange(current, current - n) {
        return true;
      }
    }
    return false;
  }
}

/// A throttled function wrapper. Ensures the inner function `f: T -> R`
/// is invoked at most once per `interval_ms` milliseconds on the
/// **leading edge**: the first call goes through immediately, then any
/// calls within the cooldown window are suppressed until the window
/// elapses. The next call after the window starts a new window.
///
/// Implementation: uses an `AtomicU64` timestamp of the last accepted
/// call plus an `AtomicBool` "in-flight" flag so only a single call is
/// observable at a time. A background ticker task spawned via
/// `task.spawn` clears the in-flight flag after `interval_ms`, enabling
/// the next leading-edge fire.
///
/// T = argument type, R = return type (both must be `Send`).
struct Throttled<T: Send, R: Send> {
  func: own<(own<T>) -> own<R>>,
  interval_ms: u64,
  /// Monotonic ms of the last accepted call; 0 if never called.
  last_call_ms: own<AtomicU64>,
  /// True while we're still inside the current cooldown window.
  in_flight: own<AtomicBool>,
  /// Most recent pending result, if a caller wants to flush().
  pending_arg: own<Mutex<Option<own<T>>>>,
}

impl<T: Send, R: Send> Throttled<T, R> {
  /// Create a new throttled wrapper. Leading-edge throttling is the default.
  fn new(f: own<(own<T>) -> own<R>>, interval_ms: u64): own<Throttled<T, R>> {
    return Throttled {
      func: f,
      interval_ms: interval_ms,
      last_call_ms: AtomicU64::new(0),
      in_flight: AtomicBool::new(false),
      pending_arg: Mutex::new(Option::None),
    };
  }

  /// Call the throttled function. Returns `Some(result)` if the call
  /// fired on the leading edge, `None` if it was dropped because the
  /// current cooldown window is still active.
  fn call(ref<Self>, arg: own<T>): Option<own<R>> {
    let now = current_time_ms();
    let last = self.last_call_ms.load();

    // Leading-edge fire: allow if we've never fired, or the window elapsed.
    let can_fire = last == 0 || (now - last) >= self.interval_ms;

    if can_fire {
      // CAS guards against concurrent callers racing on the same window.
      if self.last_call_ms.compare_exchange(last, now) {
        self.in_flight.store(true);

        // Spawn a ticker task that clears in_flight after interval_ms.
        // This uses the task runtime (task.spawn) to release the gate
        // asynchronously, so subsequent calls observe the cleared flag
        // at the right moment for the next leading edge.
        let interval = self.interval_ms;
        let flag_ref = ref self.in_flight;
        task.spawn(|| {
          sleep_ms(interval);
          flag_ref.store(false);
        });

        let result = (self.func)(arg);
        return Option::Some(result);
      }
    }

    // Drop the call, but stash the argument so flush() can fire the
    // trailing edge with the most-recent arg if the caller wants.
    let guard = self.pending_arg.lock();
    *guard = Option::Some(arg);
    return Option::None;
  }

  /// Force-execute the most recent dropped call immediately and reset
  /// the cooldown window. No-op if there is no pending argument.
  fn flush(ref<Self>): Option<own<R>> {
    let guard = self.pending_arg.lock();
    let pending = *guard;
    *guard = Option::None;
    match pending {
      Option::Some(arg) => {
        self.last_call_ms.store(current_time_ms());
        self.in_flight.store(false);
        let r = (self.func)(arg);
        return Option::Some(r);
      },
      Option::None => { return Option::None; },
    }
  }

  /// Reset the throttle window so the next call fires immediately.
  fn reset(ref<Self>): void {
    self.last_call_ms.store(0);
    self.in_flight.store(false);
    let guard = self.pending_arg.lock();
    *guard = Option::None;
  }

  /// Return true if the cooldown window is currently active.
  fn is_throttled(ref<Self>): bool {
    return self.in_flight.load();
  }
}

/// Convenience free function: wrap `f` with leading-edge throttling.
function throttle<T: Send, R: Send>(
  f: own<(own<T>) -> own<R>>,
  interval_ms: u64,
): own<Throttled<T, R>> {
  return Throttled::new(f, interval_ms);
}

// --- Runtime-provided timer functions ---

@extern("env", "__asc_current_time_ms")
declare function current_time_ms(): u64;

@extern("env", "__asc_sleep_ms")
declare function sleep_ms(ms: u64): void;
