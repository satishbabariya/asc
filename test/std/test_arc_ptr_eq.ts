// RUN: %asc check %s
// Test: Arc::ptr_eq — two clones of the same Arc share a pointer;
//       two independent Arcs of equal value do not.
function main(): i32 {
  const a = Arc::new(42);
  const a2 = a.clone();
  if !Arc::ptr_eq(&a, &a2) { return 1; }

  const b = Arc::new(42);
  if Arc::ptr_eq(&a, &b) { return 2; }

  return 0;
}
