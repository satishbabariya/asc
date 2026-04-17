// RUN: %asc check %s
// Test: Arc::make_mut returns a mutable ref; clones the inner value when shared.
function main(): i32 {
  let a = Arc::new(10);
  const m = a.make_mut();
  if m.clone() != 10 { return 1; }

  let b = Arc::new(20);
  const b2 = b.clone();
  const bm = b.make_mut();
  if bm.clone() != 20 { return 2; }

  return 0;
}
