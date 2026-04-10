// RUN: %asc check %s

function test_if_let(opt: Option<i32>): i32 {
  if let Option::Some(v) = opt {
    return 0;
  } else {
    return 1;
  }
}

function main(): i32 {
  return 0;
}
