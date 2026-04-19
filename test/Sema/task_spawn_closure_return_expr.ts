// RUN: not %asc check %s 2>&1 | FileCheck %s
// CHECK: error[E006]
// CHECK-SAME: not Send

// Regression: `return x;` inside a closure body must collect `x` as a free
// variable so Sema's Send validator can reject non-Send captures through
// the return path. Without ReturnStmt handling in collectFreeVars, this
// would silently pass Sema and then corrupt the spawned thread's env struct
// because `x` would be omitted from the capture env synthesis.
function main(): i32 {
  let rc: own<Rc<i32>> = Rc::new(42);
  let h = task.spawn(() => {
    return rc;
  });
  task.join(h);
  return 0;
}
