// RUN: %asc check %s
// test 20: closure captures
function apply(f: (i32) -> i32, x: i32): i32 {
  return f(x);
}

function main(): i32 {
  let offset: i32 = 10;
  let add_offset = (x: i32): i32 => x + offset;
  return apply(add_offset, 32);
}
