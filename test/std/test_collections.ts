// Test: Collection types.
function main(): i32 {
  // VecDeque basic operations.
  let dq: VecDeque<i32> = VecDeque::new();
  dq.push_back(1);
  dq.push_back(2);
  dq.push_front(0);
  assert_eq!(dq.len(), 3);

  const front = dq.pop_front().unwrap();
  assert_eq!(front, 0);
  const back = dq.pop_back().unwrap();
  assert_eq!(back, 2);

  return 0;
}
