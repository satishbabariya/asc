// RUN: %asc check %s

function main(): i32 {
  let m: HashMap<i32, i32> = HashMap::new();
  m.insert(1, 100);
  m.insert(2, 200);
  return m.len() as i32;
}
