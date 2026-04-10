// RUN: %asc check %s

function get_value(opt: Option<i32>): i32 {
  let v: i32 = 0;
  let Option::Some(inner) = opt else {
    return 0;
  };
  return v;
}

function main(): i32 {
  return 0;
}
