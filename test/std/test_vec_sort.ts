// RUN: %asc check %s
// Test: Vec sort, retain, truncate, iter.
function main(): i32 {
  // sort.
  let v: Vec<i32> = Vec::new();
  v.push(5);
  v.push(2);
  v.push(8);
  v.push(1);
  v.push(3);
  v.sort();
  assert_eq!(*v.get(0).unwrap(), 1);
  assert_eq!(*v.get(1).unwrap(), 2);
  assert_eq!(*v.get(2).unwrap(), 3);
  assert_eq!(*v.get(3).unwrap(), 5);
  assert_eq!(*v.get(4).unwrap(), 8);

  // retain — keep only even numbers.
  v.retain((x: ref<i32>) => *x % 2 == 0);
  assert_eq!(v.len(), 2);
  assert_eq!(*v.get(0).unwrap(), 2);
  assert_eq!(*v.get(1).unwrap(), 8);

  // truncate.
  v.push(10);
  v.push(12);
  v.truncate(2);
  assert_eq!(v.len(), 2);

  // truncate to larger — no-op.
  v.truncate(100);
  assert_eq!(v.len(), 2);

  // iter — verify it produces an iterator with correct count.
  let v2: Vec<i32> = Vec::new();
  v2.push(1);
  v2.push(2);
  v2.push(3);
  let count: i32 = 0;
  let it = v2.iter();
  loop {
    match it.next() {
      Option::Some(val) => { count = count + 1; },
      Option::None => { break; },
    }
  }
  assert_eq!(count, 3);

  return 0;
}
