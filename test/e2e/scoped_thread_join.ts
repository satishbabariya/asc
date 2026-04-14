// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "pthread_create" %t.out
// RUN: grep -q "pthread_join" %t.out
// Test: task.scope { } block auto-joins all spawned threads at scope exit.
// This is the scoped-thread pattern from RFC-0007: threads spawned inside
// the scope are guaranteed to complete before the scope exits, making it
// safe for them to borrow from the parent stack frame.

function worker(x: i32): i32 {
  return x * 2;
}

function helper(y: i32): i32 {
  return y + 10;
}

function main(): i32 {
  task.scope {
    let h1 = task.spawn(worker, 5);
    let h2 = task.spawn(helper, 3);
  }
  return 0;
}
