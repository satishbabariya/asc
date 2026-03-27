// Test: HashMap basic operations.

function main(): i32 {
  let m = HashMap::new();
  m.insert(1, 100);
  m.insert(2, 200);
  m.insert(3, 300);

  let has_2: i32 = m.contains(2);
  let has_5: i32 = m.contains(5);

  // has_2 = 1, has_5 = 0
  return has_2 + has_5;
}
