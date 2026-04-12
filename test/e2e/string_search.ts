// RUN: %asc check %s

function main(): i32 {
  let s: String = String::new();
  s.push_str("Hello World");
  let upper = s.to_uppercase();
  let lower = s.to_lowercase();
  return s.len() as i32;
}
