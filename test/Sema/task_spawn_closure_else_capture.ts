// RUN: not %asc check %s 2>&1 | FileCheck %s
// CHECK: error[E006]
// CHECK-SAME: not Send

// Regression: free variables referenced only in the else-branch of an if
// inside a closure body must be validated for Send. Without else-branch
// walking in collectFreeVars, the `rc` reference would silently escape
// Send validation and be omitted from the spawned thread's env struct.
function main(): i32 {
  let rc: own<Rc<i32>> = Rc::new(42);
  let flag: bool = true;
  let h = task.spawn(() => {
    if (flag) {
      let _x: i32 = 1;
    } else {
      let _use = rc;
    }
  });
  task.join(h);
  return 0;
}
