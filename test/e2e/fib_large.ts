// Test: iterative fibonacci for large n.

function fib(n: i32): i32 {
  let a: i32 = 0;
  let b: i32 = 1;
  let i: i32 = 0;
  while i < n {
    const tmp = a + b;
    a = b;
    b = tmp;
    i = i + 1;
  }
  return a;
}

function main(): i32 {
  const result = fib(30);
  if result == 832040 { return 0; }
  return 1;
}
// Expected: exit 0
