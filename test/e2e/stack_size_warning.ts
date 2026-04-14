// RUN: %asc build %s --emit mlir > %t.out 2>&1; grep -q "stack_size" %t.out
// Test: spawned task gets stack_size attribute from StackSizeAnalysis pass.

function big_stack(): i32 {
  let a: i32 = 1;
  let b: i32 = 2;
  let c: i32 = 3;
  return a + b + c;
}

function main(): i32 {
  let h = task.spawn(big_stack);
  task.join(h);
  return 0;
}
