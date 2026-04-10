// RUN: %asc check %s
function is_prime(n: i32): bool {
  if n <= 1 { return false; }
  let i: i32 = 2;
  while i * i <= n { if n % i == 0 { return false; } i = i + 1; }
  return true;
}
function main(): i32 {
  let count: i32 = 0; let i: i32 = 2;
  while i < 30 { if is_prime(i) { count = count + 1; } i = i + 1; }
  return count;
}
