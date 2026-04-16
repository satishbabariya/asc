// RUN: %asc check %s
// Test: String iterator methods — chars, lines, bytes.
function main(): i32 {
  // bytes
  let s: String = String::new();
  s.push_str("hello");
  let byte_count: i32 = 0;
  let bytes_iter = s.bytes();
  loop {
    match bytes_iter.next() {
      Option::Some(_) => { byte_count = byte_count + 1; },
      Option::None => { break; },
    }
  }
  assert_eq!(byte_count, 5);

  // chars (ASCII)
  let s2: String = String::new();
  s2.push_str("abc");
  let char_count: i32 = 0;
  let chars_iter = s2.chars();
  loop {
    match chars_iter.next() {
      Option::Some(_) => { char_count = char_count + 1; },
      Option::None => { break; },
    }
  }
  assert_eq!(char_count, 3);

  // lines
  let s3: String = String::new();
  s3.push_str("line1\nline2\nline3");
  let line_count: i32 = 0;
  let lines_iter = s3.lines();
  loop {
    match lines_iter.next() {
      Option::Some(_) => { line_count = line_count + 1; },
      Option::None => { break; },
    }
  }
  assert_eq!(line_count, 3);

  // into_bytes
  let s4: String = String::new();
  s4.push_str("hi");
  const bytes_vec = s4.into_bytes();
  assert_eq!(bytes_vec.len(), 2);

  return 0;
}
