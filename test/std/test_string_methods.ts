// RUN: %asc check %s
// Test: String find, trim, split, replace.
function main(): i32 {
  let s: String = String::new();
  s.push_str("  hello world  ");

  // trim.
  const trimmed = s.trim();
  assert_eq!(trimmed.len(), 11);

  // trim_start.
  const ts = s.trim_start();
  assert_eq!(ts.len(), 13);

  // trim_end.
  const te = s.trim_end();
  assert_eq!(te.len(), 13);

  // find.
  let s2: String = String::new();
  s2.push_str("abcdef");
  assert_eq!(s2.find("cd").unwrap(), 2);
  assert!(s2.find("xyz").is_none());
  assert_eq!(s2.find("a").unwrap(), 0);

  // split.
  let s3: String = String::new();
  s3.push_str("a,b,c");
  const parts = s3.split(",");
  assert_eq!(parts.len(), 3);

  // replace.
  let s4: String = String::new();
  s4.push_str("hello world");
  const replaced = s4.replace("world", "asc");
  assert_eq!(replaced.len(), 9);

  return 0;
}
