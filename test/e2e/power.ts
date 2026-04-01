function power(base: i32, exp: i32): i32 {
  let result: i32 = 1;
  let i: i32 = 0;
  while i < exp { result = result * base; i = i + 1; }
  return result;
}
function main(): i32 { if power(2, 10) == 1024 { return 0; } return 1; }
