// RUN: %asc check %s

function main(): i32 {
  let v: Vec<i32> = Vec::new();
  v.push(3);
  v.push(1);
  v.push(2);
  v.reverse();
  return v.len() as i32;
}
