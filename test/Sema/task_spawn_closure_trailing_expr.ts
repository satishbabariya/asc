// RUN: not %asc check %s 2>&1 | FileCheck %s
// CHECK: error[E006]
// CHECK-SAME: not Send

// Regression: the trailing expression of a block (`{ ...; expr }` with no
// semicolon after the final expr) must be walked for free variables. Before
// this fix, only top-level ExprStmts were visited, so a trailing reference
// to a non-Send capture would slip past Sema and then be omitted from the
// spawned thread's capture env struct.
function main(): i32 {
  let rc: own<Rc<i32>> = Rc::new(42);
  let h = task.spawn(() => {
    let _x: i32 = 1;
    rc
  });
  task.join(h);
  return 0;
}
