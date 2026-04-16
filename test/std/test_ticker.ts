// RUN: %asc check %s
// Test: Ticker periodic timing.
function main(): i32 {
  let ticker = Ticker::new(100);
  assert!(!ticker.stopped);
  ticker.reset();
  assert!(!ticker.stopped);
  ticker.stop();
  assert!(ticker.stopped);
  return 0;
}
