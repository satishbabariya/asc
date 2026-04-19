// RUN: %asc check %s 2>&1 | FileCheck %s --allow-empty
// CHECK-NOT: error

// Primitive captures are trivially Send — the canonical positive case.
// Today this is rejected by Sema (see spawn_closure_literal_rejected.ts);
// once closure-literal captures are implemented, this test should pass.
function main(): i32 {
  let x: i32 = 42;
  let h = task.spawn(() => {
    let y: i32 = x + 1;
  });
  task.join(h);
  return 0;
}
