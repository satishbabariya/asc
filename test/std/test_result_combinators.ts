// RUN: %asc check %s
// Test: new Result combinators compile (map_or, map_or_else, expect,
// expect_err, is_ok_and, is_err_and).
function double(x: own<i32>): own<i32> { return x * 2; }
function from_err(e: own<i32>): own<i32> { return e + 100; }
function positive(x: ref<i32>): bool { return *x > 0; }

function main(): i32 {
  const a: Result<i32, i32> = Result::Ok(5);
  const ma = a.map_or(0, double);
  if ma != 10 { return 1; }

  const b: Result<i32, i32> = Result::Err(7);
  const mb = b.map_or_else(from_err, double);
  if mb != 107 { return 2; }

  const c: Result<i32, i32> = Result::Ok(3);
  if !c.is_ok_and(positive) { return 3; }

  const d: Result<i32, i32> = Result::Err(2);
  if !d.is_err_and(positive) { return 4; }

  return 0;
}
