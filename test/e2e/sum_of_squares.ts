// RUN: %asc check %s
function main(): i32 {
  let sum: i32 = 0;
  for (const i of 1..=5) { sum = sum + i * i; }
  return sum;
}
