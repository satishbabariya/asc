// Tests that i8 cannot be assigned a u64 value without explicit cast.
// RUN: %asc check %s > %t.out 2>&1; grep -q "type mismatch" %t.out

function main(): i32 {
  let big: u64 = 1000;
  let small: i8 = big;
  return 0;
}
