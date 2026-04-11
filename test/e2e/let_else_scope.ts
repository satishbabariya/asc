// RUN: %asc check %s

function get_value(opt: Option<i32>): i32 {
  let Option::Some(v) = opt else {
    return 0;
  };
  return v;
}

function main(): i32 {
  return 0;
}
