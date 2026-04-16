// RUN: %asc check %s
// Test: Iterator combinators — max, min, peekable.
function main(): i32 {
  // max/min via Vec iterator.
  let v: Vec<i32> = Vec::new();
  v.push(3);
  v.push(1);
  v.push(4);
  v.push(1);
  v.push(5);

  // Test max.
  const mx = v.iter().max();
  assert!(mx.is_some());
  assert_eq!(mx.unwrap(), 5);

  // Test min.
  const mn = v.iter().min();
  assert!(mn.is_some());
  assert_eq!(mn.unwrap(), 1);

  // Test max on empty.
  let empty: Vec<i32> = Vec::new();
  assert!(empty.iter().max().is_none());

  // Test peekable.
  let v2: Vec<i32> = Vec::new();
  v2.push(10);
  v2.push(20);
  let pk = v2.iter().peekable();
  assert_eq!(pk.peek().unwrap(), 10);
  assert_eq!(pk.peek().unwrap(), 10);
  assert_eq!(pk.next().unwrap(), 10);
  assert_eq!(pk.next().unwrap(), 20);
  assert!(pk.next().is_none());

  return 0;
}
