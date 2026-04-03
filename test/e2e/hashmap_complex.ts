// Test: HashMap with many entries and lookup.

function main(): i32 {
  let m = HashMap::new();
  let i: i32 = 0;
  while i < 50 {
    m.insert(i, i * 2);
    i = i + 1;
  }
  // Verify entries 5, 10, 20 exist
  let has5: i32 = m.contains(5);
  let has10: i32 = m.contains(10);
  let has20: i32 = m.contains(20);
  let has99: i32 = m.contains(99);
  // has5=1, has10=1, has20=1, has99=0 → sum = 3
  return has5 + has10 + has20 + has99;
}
