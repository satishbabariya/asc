// RUN: %asc check %s
function main(): i32 {
  let v = Vec::new();
  v.push(1); v.push(2); v.push(3);
  let doubled = Vec::new();
  let i: i32 = 0;
  while i < 3 {
    doubled.push((i + 1) * 2);
    i = i + 1;
  }
  let sum: i32 = 0;
  for (const val of doubled) { sum = sum + val; }
  return sum;
}
