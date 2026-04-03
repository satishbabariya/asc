// Test: multiple independent channels.

function main(): i32 {
  const [tx1, rx1] = chan<i32>(4);
  const [tx2, rx2] = chan<i32>(4);

  tx1.send(10);
  tx2.send(32);

  return rx1.recv() + rx2.recv();
}
