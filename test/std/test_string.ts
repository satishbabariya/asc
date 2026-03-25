// Test: String methods.

function main(): i32 {
  // Creation.
  let s = String::new();
  assert!(s.is_empty());
  assert_eq!(s.len(), 0);

  // push_str.
  s.push_str("hello");
  assert_eq!(s.len(), 5);
  s.push_str(", world!");
  assert_eq!(s.len(), 13);

  // from.
  const greeting = String::from("hello");
  assert_eq!(greeting.len(), 5);

  // contains / starts_with / ends_with.
  assert!(greeting.contains("ell"));
  assert!(greeting.starts_with("hel"));
  assert!(greeting.ends_with("llo"));

  // to_uppercase / to_lowercase.
  const upper = greeting.to_uppercase();
  const lower = upper.to_lowercase();

  // clear.
  let mut_s = String::from("test");
  mut_s.clear();
  assert!(mut_s.is_empty());

  // repeat.
  const repeated = String::from("ab").repeat(3);
  assert_eq!(repeated.len(), 6);

  return 0;
}
