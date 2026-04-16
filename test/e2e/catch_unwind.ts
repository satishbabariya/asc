// RUN: %asc check %s
// Test: catch_unwind builtin catches panics.
function might_panic(): void {
  panic!("test panic");
}

function main(): i32 {
  const result = catch_unwind(might_panic);
  // result is i32: 0 = success, 1 = panic caught
  assert_eq!(result, 1);
  return 0;
}
