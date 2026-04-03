// Test: generic function monomorphization.

function identity<T>(x: T): T {
  return x;
}

function max_val<T>(a: T, b: T): T {
  if a > b {
    return a;
  }
  return b;
}

function main(): i32 {
  let a: i32 = identity(42);
  let b: i32 = max_val(10, 25);
  return a + b;
}
