// RUN: %asc check %s
// Test: Collection utility functions (batch 2) — validates module structure compiles.
function main(): i32 {
  // partition_point: binary search for first index where predicate is true
  let sorted: Vec<i32> = Vec::new();
  sorted.push(1);
  sorted.push(3);
  sorted.push(5);
  sorted.push(7);
  assert_eq!(sorted.len(), 4);

  // union: elements in either a or b, deduped
  let a: Vec<i32> = Vec::new();
  a.push(1);
  a.push(2);
  a.push(3);
  let b: Vec<i32> = Vec::new();
  b.push(2);
  b.push(3);
  b.push(4);
  assert_eq!(a.len(), 3);
  assert_eq!(b.len(), 3);

  // unzip: Vec of pairs into two Vecs
  let pairs: Vec<(i32, i32)> = Vec::new();
  pairs.push((1, 10));
  pairs.push((2, 20));
  assert_eq!(pairs.len(), 2);

  // zip_with: combine two Vecs with a function
  let xs: Vec<i32> = Vec::new();
  xs.push(1);
  xs.push(2);
  let ys: Vec<i32> = Vec::new();
  ys.push(10);
  ys.push(20);
  assert_eq!(xs.len(), 2);
  assert_eq!(ys.len(), 2);

  // scan_sum: cumulative sums
  let nums: Vec<i32> = Vec::new();
  nums.push(1);
  nums.push(2);
  nums.push(3);
  assert_eq!(nums.len(), 3);

  return 0;
}
