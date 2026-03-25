// std/async/debounce.ts — Debounce and throttle wrappers (RFC-0020)

import { Mutex } from '../sync/mutex';
import { AtomicU64 } from '../sync/atomic';

/// A debounced function wrapper. Delays invocation until `delay_ms` has elapsed
/// since the last call. Only the last call's arguments are used.
struct Debounced<F: Fn()> {
  func: own<F>,
  delay_ms: u64,
  timer_id: own<Mutex<u64>>,
  pending: own<Mutex<bool>>,
}

impl<F: Fn()> Debounced<F> {
  /// Create a new debounced wrapper.
  fn new(func: own<F>, delay_ms: u64): own<Debounced<F>> {
    return Debounced {
      func: func,
      delay_ms: delay_ms,
      timer_id: Mutex::new(0),
      pending: Mutex::new(false),
    };
  }

  /// Call the debounced function. Resets the timer on each call.
  fn call(ref<Self>): void {
    let timer_guard = self.timer_id.lock();
    // Cancel any pending timer.
    if *timer_guard != 0 {
      cancel_timer(*timer_guard);
    }
    // Schedule new invocation.
    let func_ref = ref self.func;
    *timer_guard = set_timer(self.delay_ms, || {
      (*func_ref)();
    });
  }

  /// Cancel any pending invocation.
  fn cancel(ref<Self>): void {
    let timer_guard = self.timer_id.lock();
    if *timer_guard != 0 {
      cancel_timer(*timer_guard);
      *timer_guard = 0;
    }
  }

  /// Immediately invoke the function and cancel any pending timer.
  fn flush(ref<Self>): void {
    self.cancel();
    (self.func)();
  }
}

/// A throttled function wrapper. Ensures the function is called at most once
/// per `interval_ms` milliseconds.
struct Throttled<F: Fn()> {
  func: own<F>,
  interval_ms: u64,
  last_call: own<AtomicU64>,
}

impl<F: Fn()> Throttled<F> {
  /// Create a new throttled wrapper.
  fn new(func: own<F>, interval_ms: u64): own<Throttled<F>> {
    return Throttled {
      func: func,
      interval_ms: interval_ms,
      last_call: AtomicU64::new(0),
    };
  }

  /// Call the throttled function. Invokes immediately if enough time has passed,
  /// otherwise the call is dropped.
  fn call(ref<Self>): bool {
    let now = current_time_ms();
    let last = self.last_call.load();
    if now - last >= self.interval_ms {
      if self.last_call.compare_exchange(last, now) {
        (self.func)();
        return true;
      }
    }
    return false;
  }

  /// Reset the throttle timer, allowing the next call through immediately.
  fn reset(ref<Self>): void {
    self.last_call.store(0);
  }
}

/// Convenience: create a debounced function.
function debounce<F: Fn()>(func: own<F>, delay_ms: u64): own<Debounced<F>> {
  return Debounced::new(func, delay_ms);
}

/// Convenience: create a throttled function.
function throttle<F: Fn()>(func: own<F>, interval_ms: u64): own<Throttled<F>> {
  return Throttled::new(func, interval_ms);
}

// --- Runtime-provided timer functions ---

@extern("env", "__asc_set_timer")
declare function set_timer(delay_ms: u64, callback: () -> void): u64;

@extern("env", "__asc_cancel_timer")
declare function cancel_timer(timer_id: u64): void;

@extern("env", "__asc_current_time_ms")
declare function current_time_ms(): u64;
