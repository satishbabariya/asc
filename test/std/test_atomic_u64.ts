// RUN: %asc check %s
// Test: AtomicU64 basic operations.
function main(): i32 {
  const a = AtomicU64::new(0);
  assert_eq!(a.load(Ordering::SeqCst), 0);
  a.store(42, Ordering::SeqCst);
  assert_eq!(a.load(Ordering::SeqCst), 42);
  const old = a.fetch_add(8, Ordering::SeqCst);
  assert_eq!(old, 42);
  assert_eq!(a.load(Ordering::SeqCst), 50);
  const swapped = a.swap(100, Ordering::SeqCst);
  assert_eq!(swapped, 50);
  assert_eq!(a.load(Ordering::SeqCst), 100);
  return 0;
}
