// Test: HashMap<K,V>.
function main(): i32 {
  let map = HashMap::new<i32, i32>();
  assert!(map.is_empty());
  map.insert(1, 100);
  map.insert(2, 200);
  map.insert(3, 300);
  assert_eq!(map.len(), 3);
  assert!(map.contains_key(&1));
  assert_eq!(*map.get(&2).unwrap(), 200);
  map.remove(&2);
  assert!(!map.contains_key(&2));
  assert_eq!(map.len(), 2);
  map.clear();
  assert!(map.is_empty());
  return 0;
}
