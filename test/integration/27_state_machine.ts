// test 27: state machine with loop construct
function run_loop(): i32 {
  let x: i32 = 0;
  loop {
    x = x + 1;
    if x >= 10 {
      return x;
    }
  }
}

function main(): i32 {
  return run_loop();
}
