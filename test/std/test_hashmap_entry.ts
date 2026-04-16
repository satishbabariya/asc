// RUN: %asc check %s
// Test: HashMap get_mut, keys, values.
function main(): i32 {
  let map: HashMap<i32, i32> = HashMap::new();
  map.insert(1, 100);
  map.insert(2, 200);
  map.insert(3, 300);

  // get_mut — modify value in place.
  let val = map.get_mut(2).unwrap();
  *val = 999;
  assert_eq!(*map.get(2).unwrap(), 999);

  // keys.
  const ks = map.keys();
  assert_eq!(ks.len(), 3);

  // values.
  const vs = map.values();
  assert_eq!(vs.len(), 3);

  return 0;
}
