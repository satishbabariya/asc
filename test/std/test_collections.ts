// Test: Collection utilities.
function main(): i32 {
  // VecDeque.
  let dq = VecDeque::new<i32>();
  dq.push_back(1);
  dq.push_back(2);
  dq.push_front(0);
  assert_eq!(dq.len(), 3);
  assert_eq!(*dq.front().unwrap(), 0);
  assert_eq!(*dq.back().unwrap(), 2);
  assert_eq!(dq.pop_front().unwrap(), 0);
  assert_eq!(dq.pop_back().unwrap(), 2);

  // HashSet.
  let set = HashSet::new<i32>();
  assert!(set.insert(1));
  assert!(set.insert(2));
  assert!(!set.insert(1)); // duplicate
  assert!(set.contains(&1));
  assert!(!set.contains(&3));
  assert_eq!(set.len(), 2);

  return 0;
}
