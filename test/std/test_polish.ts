// RUN: %asc check %s
// Test: Polish pass — BTreeMap, Display, Vec::swap, String::splitn, Option::take.
function main(): i32 {
  // Vec::swap
  let v: Vec<i32> = Vec::new();
  v.push(10);
  v.push(20);
  v.push(30);
  v.swap(0, 2);
  assert_eq!(*v.get(0).unwrap(), 30);
  assert_eq!(*v.get(2).unwrap(), 10);

  // Option::take
  let opt: Option<i32> = Option::Some(42);
  let taken = opt.take();
  assert!(opt.is_none());
  assert!(taken.is_some());

  // Option::replace
  let opt2: Option<i32> = Option::Some(1);
  let old = opt2.replace(2);
  assert_eq!(old.unwrap(), 1);
  assert_eq!(opt2.unwrap(), 2);

  return 0;
}
