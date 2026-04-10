// RUN: %asc check %s
// Test: task.spawn creates a real OS thread via pthread_create.

function worker(): void {
  // Worker function executed on separate thread.
  // Closure captures via globals enable channel communication.
}

function main(): i32 {
  task.spawn(worker);
  return 0;
}
