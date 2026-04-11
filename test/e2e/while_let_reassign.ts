// RUN: %asc check %s

function main(): i32 {
  let x: Option<i32> = Option::Some(42);
  let result: i32 = 0;
  while let Option::Some(v) = x {
    result = v;
    x = Option::None;
  }
  return result;
}
