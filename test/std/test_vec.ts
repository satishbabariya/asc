// RUN: %asc check %s
// Test: Vec<T> methods — uses compiler-supported syntax.
function main(): i32 {
  let v: Vec<i32> = Vec::new();
  assert!(v.is_empty());
  v.push(10);
  v.push(20);
  v.push(30);
  assert_eq!(v.len(), 3);

  // pop.
  const last = v.pop().unwrap();
  assert_eq!(last, 30);
  assert_eq!(v.len(), 2);

  // clear.
  v.clear();
  assert!(v.is_empty());

  return 0;
}
