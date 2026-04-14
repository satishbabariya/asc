// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "pthread_create" %t.out
// Test: task.spawn with closure captures parent variable.

function process(x: i32): i32 {
  return x * 2;
}
function main(): i32 {
  let val: i32 = 21;
  let h = task.spawn(process, val);
  task.join(h);
  return 0;
}
