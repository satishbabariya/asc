// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "__task_0_wrapper" %t.out
// RUN: grep -q "@worker_fn" %t.out
// RUN: grep -q "pthread_create" %t.out
// Test: task.spawn(named_fn) synthesizes a pthread wrapper that calls the
// worker function by symbol name. Locks in the wrapper→worker invariant so
// future HIRBuilder refactors can't silently break the spawn path.

function worker_fn(): void {
  // no-op
}

function main(): i32 {
  let h = task.spawn(worker_fn);
  task.join(h);
  return 0;
}
