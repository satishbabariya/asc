// RUN: %asc check %s
// Test: Semaphore total_permits.
function main(): i32 {
  const sem = Semaphore::new(3);
  assert_eq!(sem.total_permits(), 3);
  assert_eq!(sem.available_permits(), 3);
  const guard = sem.acquire();
  assert_eq!(sem.available_permits(), 2);
  assert_eq!(sem.total_permits(), 3);
  return 0;
}
