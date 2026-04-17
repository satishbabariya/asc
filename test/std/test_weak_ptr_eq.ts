// RUN: %asc check %s
// Test: Weak::ptr_eq on Arc and Rc derived Weaks.
function main(): i32 {
  const a = Arc::new(7);
  const w1 = a.downgrade();
  const w2 = a.downgrade();
  if !Weak::ptr_eq(&w1, &w2) { return 1; }

  const b = Arc::new(7);
  const w3 = b.downgrade();
  if Weak::ptr_eq(&w1, &w3) { return 2; }

  return 0;
}
