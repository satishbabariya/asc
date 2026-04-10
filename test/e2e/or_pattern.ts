// RUN: %asc check %s

function classify(x: i32): i32 {
  match (x) {
    1 | 2 | 3 => 10,
    4 | 5 => 20,
    _ => 0,
  }
}

function main(): i32 {
  return classify(2);
}
