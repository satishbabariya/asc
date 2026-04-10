// RUN: %asc check %s
// Test: Box<T>.
function main(): i32 {
  const b = Box::new(42);
  assert_eq!(*b.as_ref(), 42);
  const val = b.into_inner();
  assert_eq!(val, 42);
  return 0;
}
