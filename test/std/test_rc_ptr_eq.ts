// RUN: %asc check %s
// Test: Rc::ptr_eq — two clones share a pointer; independent Rcs do not.
function main(): i32 {
  const a = Rc::new(42);
  const a2 = a.clone();
  if !Rc::ptr_eq(&a, &a2) { return 1; }

  const b = Rc::new(42);
  if Rc::ptr_eq(&a, &b) { return 2; }

  return 0;
}
