// RUN: %asc check %s
// Test: Throttled<T,R> and Debounced<T,R> from std/async (RFC-0020 §4, §5).
// Leading-edge throttle fires the first call; subsequent calls within the
// cooldown window are dropped. Debounce collapses a burst into a single
// trailing-edge invocation carrying the most-recent argument.

// A simple unary worker used for both Throttled and Debounced.
function double_it(n: own<i32>): own<i32> {
  return *n * 2;
}

fn test_throttled_leading_edge(): void {
  // 10 ms window. First .call() fires on the leading edge; any calls
  // within the window are dropped and return None.
  let t = Throttled::new(double_it, 10 as u64);
  assert!(!t.is_throttled());

  // Leading-edge fire on first call.
  let r1 = t.call(3);
  assert!(r1.is_some());

  // A second call inside the window is dropped.
  let r2 = t.call(4);
  assert!(r2.is_none());

  // flush() drains the pending last-wins arg and reopens the window.
  let flushed = t.flush();
  assert!(flushed.is_some());

  // reset() re-arms the leading edge for the next call.
  t.reset();
  assert!(!t.is_throttled());
  let r3 = t.call(5);
  assert!(r3.is_some());
}

fn test_debounced_collapses_burst(): void {
  // 10 ms inactivity window. Rapid-fire calls collapse into one fire.
  let d = Debounced::new(double_it, 10 as u64);

  // Burst of three calls — only the last arg survives.
  d.call(1);
  d.call(2);
  d.call(3);
  assert!(d.is_pending());

  // flush() forces the trailing edge to fire synchronously with arg=3.
  let flushed = d.flush();
  assert!(flushed.is_some());
  assert!(!d.is_pending());

  // A fresh call after flush re-arms the debounce window.
  d.call(7);
  assert!(d.is_pending());

  // cancel() discards the pending call without firing.
  d.cancel();
  assert!(!d.is_pending());

  // flush() after cancel has nothing to fire.
  let flushed2 = d.flush();
  assert!(flushed2.is_none());
}

fn test_rate_limiter_still_works(): void {
  // RateLimiter still lives in the same module alongside Throttled.
  let rl = RateLimiter::new(2 as u32, 1000 as u64);
  assert!(rl.try_acquire());
  assert!(rl.try_acquire());
  // Third try within the same window is denied.
  assert!(!rl.try_acquire());
}

fn test_throttle_convenience(): void {
  // Throttled::new constructor form (convenience free function also
  // available at the std/async/throttle module scope as `throttle`).
  let t = Throttled::new(double_it, 10 as u64);
  let r = t.call(21);
  assert!(r.is_some());
}

function main(): i32 {
  test_throttled_leading_edge();
  test_debounced_collapses_burst();
  test_rate_limiter_still_works();
  test_throttle_convenience();
  return 0;
}
