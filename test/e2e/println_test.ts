// RUN: %asc check %s
// Test: println! with string and integer arguments.

function main(): i32 {
  println!("hello");
  println!(42);
  println!("done");
  return 0;
}
