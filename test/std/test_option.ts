// Test: Option<T> methods.
function main(): i32 {
  // Some.
  const a: Option<i32> = Option::Some(42);
  assert!(a.is_some());

  // unwrap.
  const val: i32 = Option::Some(10).unwrap();
  assert_eq!(val, 10);

  return 0;
}
