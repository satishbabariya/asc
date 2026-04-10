// Tests that non-exhaustive match on Option produces a warning.
// RUN: %asc check %s > %t.out 2>&1; grep -q "non-exhaustive" %t.out

function test(x: Option<i32>): i32 {
  match (x) {
    Option::Some(v) => v,
  }
}

function main(): i32 {
  return 0;
}
