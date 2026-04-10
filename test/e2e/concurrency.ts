// RUN: %asc check %s
// End-to-end test: task and channel concurrency (single-threaded).

function compute(x: i32): i32 {
  return x * 2;
}

function main(): i32 {
  // Channel creation and basic send/recv (single-threaded emulation).
  const [tx, rx] = chan<i32>(16);
  tx.send(42);
  let value: i32 = rx.recv();
  return value;
}
