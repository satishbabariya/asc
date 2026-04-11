// RUN: %asc check %s
// Test: task.spawn passes a second argument to the worker via void *arg.

function worker_with_arg(x: i32): void {
  // Process the argument
}

function main(): i32 {
  task.spawn(worker_with_arg, 42);
  return 0;
}
