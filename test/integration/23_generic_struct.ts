// test 23: generic struct monomorphization
struct Pair<A, B> {
  first: A,
  second: B,
}

function sum_pair(p: Pair<i32, i32>): i32 {
  return p.first + p.second;
}

function main(): i32 {
  let p = Pair<i32, i32> { first: 20, second: 22 };
  return sum_pair(p);
}
