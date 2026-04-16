// RUN: %asc check %s
// Test: HashMap Entry API.
function main(): i32 {
  let map: HashMap<i32, i32> = HashMap::new();

  // entry().or_insert: insert default when absent
  map.entry(1).or_insert(100);
  assert!(map.contains_key(1));
  assert_eq!(map.len(), 1);

  // entry().or_insert: no-op when already present
  map.entry(1).or_insert(999);
  assert_eq!(map.len(), 1);

  // and_modify + or_insert: modify existing
  map.entry(1).and_modify((v: refmut<i32>) => { *v = *v + 1; }).or_insert(0);
  // Value should now be 101 (was 100, modified +1)

  // and_modify + or_insert: insert when absent
  map.entry(2).and_modify((v: refmut<i32>) => { *v = *v + 1; }).or_insert(50);
  assert_eq!(map.len(), 2);

  // values_mut
  let vals = map.values_mut();
  assert_eq!(vals.len(), 2);

  return 0;
}
