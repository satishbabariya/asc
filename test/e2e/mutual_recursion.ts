// RUN: %asc check %s
// Test: mutual recursion between two functions.

function is_even(n: i32): bool {
  if n == 0 { return true; }
  return is_odd(n - 1);
}

function is_odd(n: i32): bool {
  if n == 0 { return false; }
  return is_even(n - 1);
}

function main(): i32 {
  if is_even(42) { return 0; }
  return 1;
}
// Expected: exit 0
