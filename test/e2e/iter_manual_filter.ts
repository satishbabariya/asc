// Test: manual filter pattern — sum only even numbers.

function main(): i32 {
  let evens = Vec::new();
  let i: i32 = 1;
  while i <= 6 {
    if i % 2 == 0 {
      evens.push(i);
    }
    i = i + 1;
  }
  let sum: i32 = 0;
  for (const val of evens) {
    sum = sum + val;
  }
  return sum;
}
