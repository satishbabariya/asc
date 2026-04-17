// RUN: %asc check %s
// Test: new Option combinators compile (map_or, map_or_else, expect,
// is_some_and, ok_or_else).
function double(x: own<i32>): own<i32> { return x * 2; }
function zero(): own<i32> { return 0; }
function gt_zero(x: ref<i32>): bool { return *x > 0; }

function main(): i32 {
  const a = Option::Some(5);
  const ma = a.map_or(0, double);
  if ma != 10 { return 1; }

  const b: Option<i32> = Option::None;
  const mb = b.map_or(0, double);
  if mb != 0 { return 2; }

  const c: Option<i32> = Option::None;
  const mc = c.map_or_else(zero, double);
  if mc != 0 { return 3; }

  const d = Option::Some(3);
  if !d.is_some_and(gt_zero) { return 4; }

  return 0;
}
