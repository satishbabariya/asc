// RUN: %asc check %s
// Test: Vec dedup removes consecutive duplicates.

function main(): i32 {
  let v: Vec<i32> = Vec::new();
  v.push(1);
  v.push(1);
  v.push(2);
  v.dedup();
  return v.len() as i32;
}
