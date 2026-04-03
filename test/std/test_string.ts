// Test: String methods.
function main(): i32 {
  // Creation.
  let s: String = String::new();
  assert!(s.is_empty());
  assert_eq!(s.len(), 0);

  // push_str.
  s.push_str("hello");
  assert_eq!(s.len(), 5);

  // clear.
  s.clear();
  assert!(s.is_empty());

  return 0;
}
