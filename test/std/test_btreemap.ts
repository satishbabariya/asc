// RUN: %asc check %s
// Test: BTreeMap<K,V> with sorted order, remove, first/last.
function main(): i32 {
  let map: BTreeMap<i32, i32> = BTreeMap::new();
  assert!(map.is_empty());

  // Insert out of order — must maintain sorted order internally.
  map.insert(30, 300);
  map.insert(10, 100);
  map.insert(20, 200);
  assert_eq!(map.len(), 3);

  // Get existing key.
  assert_eq!(map.get(10).unwrap(), 100);
  assert_eq!(map.get(20).unwrap(), 200);
  assert_eq!(map.get(30).unwrap(), 300);

  // Get non-existing key.
  assert!(map.get(15).is_none());

  // Contains.
  assert!(map.contains_key(10));
  assert!(!map.contains_key(99));

  // First / last key-value.
  const first = map.first_key_value().unwrap();
  assert_eq!(first.0, 10);
  assert_eq!(first.1, 100);

  const last = map.last_key_value().unwrap();
  assert_eq!(last.0, 30);
  assert_eq!(last.1, 300);

  // Duplicate key — update value.
  const old = map.insert(20, 999);
  assert_eq!(old.unwrap(), 200);
  assert_eq!(map.len(), 3);
  assert_eq!(map.get(20).unwrap(), 999);

  // Remove.
  const removed = map.remove(20);
  assert_eq!(removed.unwrap(), 999);
  assert_eq!(map.len(), 2);
  assert!(map.get(20).is_none());

  // Remove non-existing.
  assert!(map.remove(20).is_none());

  // Clear.
  map.clear();
  assert!(map.is_empty());

  return 0;
}
