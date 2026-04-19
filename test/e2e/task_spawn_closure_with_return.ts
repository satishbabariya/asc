// RUN: %asc check %s 2>&1 | FileCheck %s --allow-empty
// CHECK-NOT: error:

// Regression: closure body with a valueless `return;` lowers cleanly into
// __spawn_closure_N without dropping a bare `return` op into an unrelated
// outer-function block.
//
// A stronger regression — `return x + 1;` that exercises currentFunction
// type-coercion — is blocked by the ReturnStmt gap in collectFreeVars
// (captured variables in return expressions are not collected, so the
// env-struct would be short a field). Both concerns are tracked for
// RFC-0007 Phase 6 walker completeness work.
function main(): i32 {
  let x: i32 = 10;
  let h = task.spawn(() => {
    let y: i32 = x + 1;
    return;
  });
  task.join(h);
  return 0;
}
