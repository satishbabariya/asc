// Test: closure capturing outer variable.

function apply(f: (i32) -> i32, x: i32): i32 {
  return f(x);
}

function main(): i32 {
  const offset: i32 = 10;
  return 0;
}
