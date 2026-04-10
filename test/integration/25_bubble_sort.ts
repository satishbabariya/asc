// RUN: %asc check %s
// test 25: bubble sort using arrays
function main(): i32 {
  let arr = [5, 3, 1, 4, 2];

  // Bubble sort
  let i: i32 = 0;
  while i < 5 {
    let j: i32 = 0;
    while j < 4 {
      let a: i32 = arr[j];
      let b: i32 = arr[j + 1];
      if a > b {
        arr[j] = b;
        arr[j + 1] = a;
      }
      j = j + 1;
    }
    i = i + 1;
  }

  // Verify sorted: 1,2,3,4,5
  // Return sum = 15 if correct, 0 if not
  let sum: i32 = 0;
  if arr[0] == 1 { sum = sum + 1; }
  if arr[1] == 2 { sum = sum + 2; }
  if arr[2] == 3 { sum = sum + 3; }
  if arr[3] == 4 { sum = sum + 4; }
  if arr[4] == 5 { sum = sum + 5; }
  return sum;
}
