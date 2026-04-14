// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "pthread_join" %t.out
// Test: scoped spawn joins threads before scope exits.
// task.spawn returns a handle and task.join emits pthread_join to wait.

function work(x: i32): i32 {
  return x + 1;
}
function main(): i32 {
  let h = task.spawn(work, 42);
  task.join(h);
  return 0;
}
