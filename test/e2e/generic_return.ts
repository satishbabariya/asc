// RUN: %asc check %s

struct Pair<T> { first: T, second: T }

function make_pair(a: i32, b: i32): Pair<i32> {
  return Pair { first: a, second: b };
}

function main(): i32 {
  let p = make_pair(3, 4);
  return p.first;
}
