// RUN: %asc check %s
function main(): i32 {
  let arr = [3, 7, 1, 9, 4, 6, 2, 8, 5];
  let max: i32 = arr[0]; let i: i32 = 1;
  while i < 9 { if arr[i] > max { max = arr[i]; } i = i + 1; }
  return max;
}
