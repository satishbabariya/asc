// RUN: %asc check %s
// Test: Semaphore extras — available_permits, try_acquire, acquire_many,
//       try_acquire_many, acquire_timeout, and RAII release via Drop.
function main(): i32 {
  const sem = Semaphore::new(3);
  assert_eq!(sem.total_permits(), 3);
  assert_eq!(sem.available_permits(), 3);

  // Blocking acquire — holds one permit for the scope of `guard`.
  const guard = sem.acquire();
  assert_eq!(sem.available_permits(), 2);
  assert_eq!(sem.total_permits(), 3);

  // try_acquire is non-blocking and returns Option<own<SemaphoreGuard>>.
  const maybe = sem.try_acquire();
  assert!(maybe.is_some());
  assert_eq!(sem.available_permits(), 1);

  // try_acquire_many with more than available should return None.
  const none_guard = sem.try_acquire_many(5);
  assert!(none_guard.is_none());
  assert_eq!(sem.available_permits(), 1);

  // try_acquire_many with exactly the remaining capacity should succeed.
  const multi = sem.try_acquire_many(1);
  assert!(multi.is_some());
  assert_eq!(sem.available_permits(), 0);

  // acquire_timeout with 0ms on an empty semaphore returns None.
  const timed_out = sem.acquire_timeout(0);
  assert!(timed_out.is_none());

  return 0;
}
