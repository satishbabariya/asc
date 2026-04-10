// RUN: %asc check %s
// Test: Mutex lock/unlock.

function main(): i32 {
  let m = Mutex::new();
  m.lock();
  let x: i32 = 42;
  m.unlock();
  return x;
}
