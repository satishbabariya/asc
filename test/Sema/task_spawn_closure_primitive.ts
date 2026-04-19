// RUN: %asc check %s 2>&1 | FileCheck %s --allow-empty
// CHECK-NOT: error:
// CHECK-NOT: not Send

// Primitive captures are trivially Send — the canonical positive case
// for closure-literal task.spawn (RFC-0007).
function main(): i32 {
  let x: i32 = 42;
  let h = task.spawn(() => {
    let y: i32 = x + 1;
  });
  task.join(h);
  return 0;
}
