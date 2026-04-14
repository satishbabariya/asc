// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "pthread_create" %t.out
// RUN: grep -q "getelementptr" %t.out
// Test: task.spawn with multiple captured variables packed into closure env struct.

function compute(a: i32, b: i32): i32 {
  return a + b;
}
function main(): i32 {
  let x: i32 = 10;
  let y: i32 = 20;
  let h = task.spawn(compute, x, y);
  task.join(h);
  return 0;
}
