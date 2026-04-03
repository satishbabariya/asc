// Test: panic with source location in message.

function will_not_panic(): i32 {
  let x: i32 = 42;
  if false {
    panic!("should not reach");
  }
  return x;
}

function main(): i32 {
  return will_not_panic();
}
