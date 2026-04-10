// RUN: %asc check %s
// Test: Box<T> heap allocation and dereference.

function boxed_add(a: i32, b: i32): i32 {
  let x = Box::new(a);
  let y = Box::new(b);
  return *x + *y;
}

function main(): i32 {
  return boxed_add(30, 12);
}
