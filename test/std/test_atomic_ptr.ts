// RUN: %asc check %s
// Test: AtomicPtr basic operations — stores pointer-sized addresses.
function main(): i32 {
  const addr_a = 4096 as usize;
  const addr_b = 8192 as usize;

  const ap = AtomicPtr::new(addr_a);
  assert_eq!(ap.load(Ordering::SeqCst), addr_a);

  ap.store(addr_b, Ordering::SeqCst);
  assert_eq!(ap.load(Ordering::SeqCst), addr_b);

  const old = ap.swap(addr_a, Ordering::SeqCst);
  assert_eq!(old, addr_b);
  assert_eq!(ap.load(Ordering::SeqCst), addr_a);

  const result = ap.compare_exchange(addr_a, addr_b, Ordering::SeqCst, Ordering::SeqCst);
  match result {
    Result::Ok(_) => {},
    Result::Err(_) => { return 1; },
  }
  assert_eq!(ap.load(Ordering::SeqCst), addr_b);

  return 0;
}
