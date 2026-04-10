// RUN: %asc check %s
function main(): i32 {
  let a: i32 = 10; let b: i32 = 20;
  let tmp: i32 = a; a = b; b = tmp;
  return a * 10 + b;
}
