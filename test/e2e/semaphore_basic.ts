// RUN: %asc check %s
// Test: Semaphore acquire/release.

function main(): i32 {
  let sem = Semaphore::new(2);
  sem.acquire();
  sem.acquire();
  let avail: i32 = sem.available_permits();
  sem.release();
  sem.release();
  let after: i32 = sem.available_permits();
  // avail should be 0, after should be 2
  return avail * 10 + after;
}
