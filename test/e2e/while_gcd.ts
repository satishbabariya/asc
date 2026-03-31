// Test: GCD via Euclidean algorithm.

function gcd(a: i32, b: i32): i32 {
  let x: i32 = a;
  let y: i32 = b;
  while y != 0 {
    let t: i32 = y;
    y = x % y;
    x = t;
  }
  return x;
}

function main(): i32 {
  return gcd(252, 105);
}
