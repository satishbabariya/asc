// End-to-end test: task and channel concurrency.

function compute(x: i32): i32 {
  return x * 2;
}

function main(): i32 {
  const [tx, rx] = chan<i32>(16);
  const handle = task.spawn(() => {
    const result = compute(21);
    tx.send(result);
  });
  const value = rx.recv();
  handle.join();
  return 0;
}
