// RUN: %asc check %s

function test_while_let(x: Option<i32>): i32 {
  let result: i32 = 0;
  while let Option::Some(v) = x {
    result = v;
    break;
  }
  return result;
}

function main(): i32 {
  return 0;
}
