// RUN: %asc check %s
// Test: count elements using for-in loop.

function main(): i32 {
  let v = Vec::new();
  v.push(1);
  v.push(2);
  v.push(3);
  v.push(4);
  v.push(5);
  let count: i32 = 0;
  for (const val of v) {
    count = count + 1;
  }
  return count;
}
