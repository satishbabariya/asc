// Test: match as expression returning a value.

function classify(x: i32): i32 {
  let label: i32 = match x {
    0 => 10,
    1 => 20,
    2 => 30,
    _ => 99,
  };
  return label;
}

function main(): i32 {
  let a: i32 = classify(0);
  let b: i32 = classify(2);
  return a + b;
}
