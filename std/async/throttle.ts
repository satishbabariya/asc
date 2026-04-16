// std/async/throttle.ts — Rate limiting utility (RFC-0020)

import { AtomicU64, AtomicU32 } from '../sync/atomic';

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

// --- Runtime-provided timer function ---

@extern("env", "__asc_current_time_ms")
declare function current_time_ms(): u64;
