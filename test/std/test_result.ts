// Test: Result<T,E> methods.

function main(): i32 {
  const ok: Result<i32, String> = Result::Ok(42);
  assert!(ok.is_ok());
  assert!(!ok.is_err());
  assert_eq!(ok.unwrap(), 42);

  const err: Result<i32, String> = Result::Err(String::from("oops"));
  assert!(err.is_err());

  // map.
  const doubled = Result::Ok(5).map((x: i32) => x * 2);
  assert_eq!(doubled.unwrap(), 10);

  // and_then.
  const chained = Result::Ok(10).and_then((x: i32) => {
    if x > 5 { return Result::Ok(x + 1); }
    return Result::Err("too small");
  });
  assert_eq!(chained.unwrap(), 11);

  // unwrap_or.
  const val = Result::Err("error").unwrap_or(0);
  assert_eq!(val, 0);

  return 0;
}
