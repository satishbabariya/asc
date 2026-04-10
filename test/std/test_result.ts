// RUN: %asc check %s
// Test: Result<T,E> methods.
function main(): i32 {
  const ok: Result<i32, i32> = Result::Ok(42);
  assert!(ok.is_ok());

  const err: Result<i32, i32> = Result::Err(1);
  assert!(err.is_err());

  // unwrap.
  const val: i32 = Result::Ok(42).unwrap();
  assert_eq!(val, 42);

  // unwrap_or.
  const fallback: i32 = Result::Err(1).unwrap_or(0);
  assert_eq!(fallback, 0);

  return 0;
}
