// RUN: not %asc check %s 2>&1 | FileCheck %s
// CHECK: error:
// CHECK-SAME: not Send

// Capturing an Rc into a spawned task must be rejected — Rc is non-atomic
// refcounted and fundamentally not Send (RFC-0007). The explicit
// `own<Rc<i32>>` annotation ensures the binding has a resolved Sema type
// so the free-var Send check has something to inspect; without it the
// `Rc::new` expression currently yields a null type and the check skips.
function main(): i32 {
  let rc: own<Rc<i32>> = Rc::new(42);
  let h = task.spawn(() => {
    let _x: i32 = 1;
    let _use = rc;
  });
  task.join(h);
  return 0;
}
