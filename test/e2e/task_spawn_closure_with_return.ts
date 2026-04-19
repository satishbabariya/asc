// RUN: %asc check %s 2>&1 | FileCheck %s --allow-empty
// CHECK-NOT: error:

// Regression: closure body with an explicit (valueless) `return` must lower
// against the lifted __spawn_closure_N function's `() -> void` type, not
// against the outer function's `i32` return type. Before the
// currentFunction save/restore fix, visitReturnStmt would consult the
// enclosing function and attempt to coerce the (empty) return against i32.
function main(): i32 {
  let x: i32 = 10;
  let h = task.spawn(() => {
    let y: i32 = x + 1;
    return;
  });
  task.join(h);
  return 0;
}
