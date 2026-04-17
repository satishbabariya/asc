// RUN: %asc check %s
// Test: Rc::make_mut clones when shared.
function main(): i32 {
  let a = Rc::new(10);
  const m = a.make_mut();
  if m.clone() != 10 { return 1; }

  let b = Rc::new(20);
  const b2 = b.clone();
  const bm = b.make_mut();
  if bm.clone() != 20 { return 2; }

  return 0;
}
