// RUN: %asc check %s

function main(): i32 {
  let s: String = String::new();
  s.push_str("  hello world  ");
  let trimmed = s.trim();
  let ch = s.char_at(2);
  return trimmed.len() as i32;
}
