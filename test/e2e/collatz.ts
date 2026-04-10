// RUN: %asc check %s
function collatz_steps(n: i32): i32 {
  let x: i32 = n; let steps: i32 = 0;
  while x != 1 { if x % 2 == 0 { x = x / 2; } else { x = x * 3 + 1; } steps = steps + 1; }
  return steps;
}
function main(): i32 { return collatz_steps(27); }
