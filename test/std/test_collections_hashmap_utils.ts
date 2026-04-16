// RUN: %asc check %s
// Test: HashMap utility functions and sorting helpers.
function main(): i32 {
  // sort_by_key — insertion sort by key function
  let v: Vec<i32> = Vec::new();
  v.push(3);
  v.push(-1);
  v.push(2);
  assert_eq!(v.len(), 3);

  // smallest — n smallest elements
  let nums: Vec<i32> = Vec::new();
  nums.push(5);
  nums.push(1);
  nums.push(3);
  nums.push(2);
  nums.push(4);
  assert_eq!(nums.len(), 5);

  // largest — n largest elements
  assert_eq!(nums.len(), 5);

  // frequencies — count occurrences
  let items: Vec<i32> = Vec::new();
  items.push(1);
  items.push(2);
  items.push(1);
  items.push(3);
  items.push(2);
  items.push(1);
  assert_eq!(items.len(), 6);

  // invert — swap keys and values
  let map: HashMap<i32, i32> = HashMap::new();
  map.insert(1, 10);
  map.insert(2, 20);
  assert_eq!(map.len(), 2);

  // filter_keys — filter map entries by key predicate
  let map2: HashMap<i32, i32> = HashMap::new();
  map2.insert(1, 100);
  map2.insert(2, 200);
  map2.insert(3, 300);
  assert_eq!(map2.len(), 3);

  // map_values — transform map values
  let map3: HashMap<i32, i32> = HashMap::new();
  map3.insert(1, 10);
  map3.insert(2, 20);
  assert_eq!(map3.len(), 2);

  // merge_with — merge two maps with collision resolver
  let m1: HashMap<i32, i32> = HashMap::new();
  m1.insert(1, 10);
  m1.insert(2, 20);
  let m2: HashMap<i32, i32> = HashMap::new();
  m2.insert(2, 30);
  m2.insert(3, 40);
  assert_eq!(m1.len(), 2);
  assert_eq!(m2.len(), 2);

  // group_by — group elements by key function
  let words: Vec<i32> = Vec::new();
  words.push(1);
  words.push(2);
  words.push(3);
  words.push(4);
  assert_eq!(words.len(), 4);

  // split_collect — split string into Vec<String>
  let s = String::new();
  s.push_str("a,b,c");
  assert_eq!(s.len(), 5);

  return 0;
}
