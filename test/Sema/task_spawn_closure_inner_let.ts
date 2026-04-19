// RUN: %asc check %s 2>&1 | FileCheck %s --allow-empty
// CHECK-NOT: error:
// CHECK-NOT: not Send

// Regression: inner `let`-bound names inside a task.spawn closure must
// not be looked up in the outer scope when the free-var walker collects
// captures. Before this fix the BlockExpr branch only handled ExprStmts
// and never extended `boundNames` with names introduced by LetStmts, so
// a reference to an inner let-bound name would escape as a false
// "capture" and be validated against an unrelated outer binding of the
// same name — producing spurious Send errors.
//
// Here `tmp` is introduced by an outer `let` and then shadowed by an
// inner `let` inside the closure body; the subsequent reference to
// `tmp` resolves to the inner binding and must not be treated as a
// capture of the outer one.
function main(): i32 {
  let tmp: i32 = 100;
  let h = task.spawn(() => {
    let tmp: i32 = 5;
    tmp;
  });
  task.join(h);
  return 0;
}
