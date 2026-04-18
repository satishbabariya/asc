// RUN: %asc check %s > %t.out 2>&1; grep -q "closure literal" %t.out
// RUN: %asc check %s > %t.out 2>&1; grep -q "task.spawn" %t.out
// Test: task.spawn with a closure literal as first arg is rejected by Sema
// rather than silently miscompiling (the old behavior dropped the pthread_create
// call and left a null pthread_t to join, which would segfault at runtime).
// Named functions remain the supported form; see task_spawn_arg.ts.

function main(): i32 {
  let val: i32 = 21;
  let h = task.spawn(() => {
    return val * 2;
  });
  task.join(h);
  return 0;
}
