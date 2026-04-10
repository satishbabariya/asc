// Tests that ? operator unwraps Result<T,E> to T.
// RUN: %asc check %s 2>&1

function may_fail(): Result<i32, String> {
  return Result::Ok(42);
}

function caller(): Result<i32, String> {
  let val = may_fail()?;
  return Result::Ok(val);
}

function main(): i32 {
  return 0;
}
