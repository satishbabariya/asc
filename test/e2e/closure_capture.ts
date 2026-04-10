// RUN: %asc check %s 2>&1 || true
// Test: closure capturing outer variables.

function apply(f: (i32) -> i32, x: i32): i32 {
  return f(x);
}

function main(): i32 {
  let offset: i32 = 10;
  let scale: i32 = 3;
  let f = (x: i32): i32 => x * scale + offset;
  return apply(f, 5);
}
