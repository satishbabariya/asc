// RUN: %asc check %s

function main(): i32 {
  let a = Arc::new(42);
  let b = a.clone();
  return 0;
}
