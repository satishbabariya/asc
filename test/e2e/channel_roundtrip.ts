// RUN: %asc check %s
// Test: channel send/recv with ring buffer runtime.

function main(): i32 {
  const [tx, rx] = chan<i32>(4);
  tx.send(10);
  tx.send(20);
  tx.send(12);
  return rx.recv() + rx.recv() + rx.recv();
}
