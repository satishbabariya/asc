// Test: Option<T> methods.

function main(): i32 {
  // Some and None.
  const a: Option<i32> = Option::Some(42);
  assert!(a.is_some());
  assert!(!a.is_none());

  const b: Option<i32> = Option::None;
  assert!(b.is_none());
  assert!(!b.is_some());

  // unwrap.
  const val = Option::Some(10).unwrap();
  assert_eq!(val, 10);

  // unwrap_or.
  const c = Option::None.unwrap_or(99);
  assert_eq!(c, 99);

  // map.
  const d = Option::Some(5).map((x: i32) => x * 2);
  assert_eq!(d.unwrap(), 10);

  // and_then.
  const e = Option::Some(3).and_then((x: i32) => {
    if x > 0 { return Option::Some(x + 1); }
    return Option::None;
  });
  assert_eq!(e.unwrap(), 4);

  // filter.
  const f = Option::Some(7).filter((x: ref<i32>) => *x > 5);
  assert!(f.is_some());
  const g = Option::Some(3).filter((x: ref<i32>) => *x > 5);
  assert!(g.is_none());

  return 0;
}
