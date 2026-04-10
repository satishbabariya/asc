// RUN: %asc check %s
// test 26: Box<T> and linked list
function main(): i32 {
  let a = Box::new(10);
  let b = Box::new(20);
  let c = Box::new(12);
  return *a + *b + *c;
}
