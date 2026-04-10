// RUN: %asc check %s
function fizzbuzz_sum(n: i32): i32 {
  let sum: i32 = 0; let i: i32 = 1;
  while i <= n {
    let val: i32 = 0;
    if i % 15 == 0 { val = 3; }
    else { if i % 3 == 0 { val = 1; } else { if i % 5 == 0 { val = 2; } } }
    sum = sum + val; i = i + 1;
  }
  return sum;
}
function main(): i32 { return fizzbuzz_sum(15); }
