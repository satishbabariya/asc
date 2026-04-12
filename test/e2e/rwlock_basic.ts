// RUN: %asc check %s
// Test: RwLock read/write lock basic usage.

function main(): i32 {
  let rw = RwLock::new();
  rw.read_lock();
  rw.read_unlock();
  rw.write_lock();
  rw.write_unlock();
  return 0;
}
