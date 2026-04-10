// RUN: %asc check %s

function double(x: i32): i32 { return x * 2; }
function is_even(x: i32): i32 { return (x % 2 == 0) as i32; }
function add(acc: i32, x: i32): i32 { return acc + x; }

function main(): i32 {
  let v: Vec<i32> = Vec::new();
  v.push(1);
  v.push(2);
  v.push(3);
  v.push(4);

  let sum = v.fold(0, add);
  return sum;
}
