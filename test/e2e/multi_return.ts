function classify(n: i32): i32 {
  if n < 0 { return -1; }
  if n == 0 { return 0; }
  if n < 10 { return 1; }
  if n < 100 { return 2; }
  return 3;
}
function main(): i32 { return classify(-5) + classify(0) + classify(7) + classify(42) + classify(999); }
