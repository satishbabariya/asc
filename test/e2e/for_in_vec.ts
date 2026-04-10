// RUN: %asc check %s
// Test: for-in loop over Vec with iterator protocol.

function main(): i32 {
  let v = Vec::new();
  v.push(10);
  v.push(20);
  v.push(12);

  let sum: i32 = 0;
  for (const val of v) {
    sum = sum + val;
  }
  return sum;
}
