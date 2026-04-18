// std/async/semaphore.ts — Counting semaphore with RAII guard (RFC-0020)

import { Mutex } from '../sync/mutex';
import { CondVar } from '../sync/condvar';

/// A counting semaphore that limits concurrent access to a resource.
struct Semaphore {
  count: own<Mutex<usize>>,
  max_permits: usize,
  condvar: own<CondVar>,
}

impl Semaphore {
  /// Create a new semaphore with the given number of permits.
  fn new(permits: usize): own<Semaphore> {
    return Semaphore {
      count: Mutex::new(permits),
      max_permits: permits,
      condvar: CondVar::new(),
    };
  }

  /// Acquire a permit, blocking until one is available.
  /// Returns a SemaphoreGuard that releases the permit on drop.
  fn acquire(ref<Self>): own<SemaphoreGuard> {
    let guard = self.count.lock();
    while *guard == 0 {
      guard = self.condvar.wait(guard);
    }
    *guard = *guard - 1;
    return SemaphoreGuard { semaphore: self };
  }

  /// Try to acquire a permit without blocking.
  /// Returns None if no permits are available.
  fn try_acquire(ref<Self>): Option<own<SemaphoreGuard>> {
    let guard = self.count.lock();
    if *guard == 0 {
      return Option::None;
    }
    *guard = *guard - 1;
    return Option::Some(SemaphoreGuard { semaphore: self });
  }

  /// Release a permit back to the semaphore.
  fn release(ref<Self>): void {
    let guard = self.count.lock();
    assert!(*guard < self.max_permits);
    *guard = *guard + 1;
    self.condvar.notify_one();
  }

  /// Get the number of currently available permits.
  fn available_permits(ref<Self>): usize {
    let guard = self.count.lock();
    return *guard;
  }

  /// Get the total number of permits (initial capacity).
  fn total_permits(ref<Self>): usize {
    return self.max_permits;
  }

  /// Acquire a permit with a timeout in milliseconds.
  /// Returns None if timeout expires before a permit is available.
  fn acquire_timeout(ref<Self>, timeout_ms: u64): Option<own<SemaphoreGuard>> {
    @extern("__asc_clock_monotonic")
    const start_ns = clock_monotonic();
    const deadline_ns = start_ns + timeout_ms * 1_000_000;

    loop {
      match self.try_acquire() {
        Option::Some(guard) => { return Option::Some(guard); },
        Option::None => {},
      }

      @extern("__asc_clock_monotonic")
      const now = clock_monotonic();
      if now >= deadline_ns {
        return Option::None;
      }
    }
  }

  /// Acquire `n` permits at once, blocking until all are available.
  /// Returns a MultiSemaphoreGuard that releases all `n` permits on drop.
  /// The acquisition is atomic — either all `n` permits are taken together or none.
  fn acquire_many(ref<Self>, n: usize): own<MultiSemaphoreGuard> {
    let guard = self.count.lock();
    while *guard < n {
      guard = self.condvar.wait(guard);
    }
    *guard = *guard - n;
    return MultiSemaphoreGuard { semaphore: self, permits: n };
  }

  /// Try to acquire `n` permits atomically without blocking.
  /// Returns None if fewer than `n` permits are currently available.
  fn try_acquire_many(ref<Self>, n: usize): Option<own<MultiSemaphoreGuard>> {
    let guard = self.count.lock();
    if *guard < n {
      return Option::None;
    }
    *guard = *guard - n;
    return Option::Some(MultiSemaphoreGuard { semaphore: self, permits: n });
  }

  /// Release `n` permits back to the semaphore.
  fn release_many(ref<Self>, n: usize): void {
    let guard = self.count.lock();
    assert!(*guard + n <= self.max_permits);
    *guard = *guard + n;
    self.condvar.notify_all();
  }
}

/// RAII guard that releases a semaphore permit when dropped.
struct SemaphoreGuard {
  semaphore: ref<Semaphore>,
}

impl Drop for SemaphoreGuard {
  fn drop(refmut<Self>): void {
    self.semaphore.release();
  }
}

/// RAII guard that releases multiple semaphore permits when dropped.
/// Produced by `Semaphore::acquire_many` / `try_acquire_many`.
struct MultiSemaphoreGuard {
  semaphore: ref<Semaphore>,
  permits: usize,
}

impl MultiSemaphoreGuard {
  /// Returns the number of permits held by this guard.
  fn permits(ref<Self>): usize {
    return self.permits;
  }
}

impl Drop for MultiSemaphoreGuard {
  fn drop(refmut<Self>): void {
    self.semaphore.release_many(self.permits);
  }
}

/// A binary semaphore (mutex-like, 0 or 1 permits).
struct BinarySemaphore {
  inner: own<Semaphore>,
}

impl BinarySemaphore {
  fn new(): own<BinarySemaphore> {
    return BinarySemaphore { inner: Semaphore::new(1) };
  }

  fn acquire(ref<Self>): own<SemaphoreGuard> {
    return self.inner.acquire();
  }

  fn try_acquire(ref<Self>): Option<own<SemaphoreGuard>> {
    return self.inner.try_acquire();
  }

  fn release(ref<Self>): void {
    self.inner.release();
  }
}
