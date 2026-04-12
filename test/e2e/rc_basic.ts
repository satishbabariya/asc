// RUN: %asc check %s

function main(): i32 {
  let a = Rc::new(42);
  let b = a.clone();
  return 0;
}
