// Test: Vec<T> methods.
function main(): i32 {
  let v = Vec::new<i32>();
  assert!(v.is_empty());
  v.push(10);
  v.push(20);
  v.push(30);
  assert_eq!(v.len(), 3);
  assert_eq!(*v.get(0).unwrap(), 10);
  assert_eq!(*v.get(1).unwrap(), 20);
  assert_eq!(*v.get(2).unwrap(), 30);
  assert!(v.get(3).is_none());

  // pop.
  const last = v.pop().unwrap();
  assert_eq!(last, 30);
  assert_eq!(v.len(), 2);

  // contains.
  assert!(v.contains(&10));
  assert!(!v.contains(&99));

  // insert / remove.
  v.insert(1, 15);
  assert_eq!(v.len(), 3);
  assert_eq!(*v.get(1).unwrap(), 15);
  const removed = v.remove(1);
  assert_eq!(removed, 15);

  // reverse.
  v.push(30);
  v.push(40);
  v.reverse();
  assert_eq!(*v.get(0).unwrap(), 40);

  // clear.
  v.clear();
  assert!(v.is_empty());

  return 0;
}
