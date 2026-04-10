// RUN: %asc check %s
// Test: array literal with indexing.

function get_second(): i32 {
  let arr = [10, 20, 30];
  return arr[1];
}

function sum_array(): i32 {
  let a = [5, 10, 15, 20];
  let total: i32 = 0;
  total = a[0] + a[1] + a[2] + a[3];
  return total;
}

function main(): i32 {
  return get_second() + sum_array();
}
