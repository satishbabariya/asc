// Test enum and match expressions.

enum Direction {
  North,
  South,
  East,
  West,
}

function to_number(d: Direction): i32 {
  match d {
    0 => 1,
    1 => 2,
    2 => 3,
    _ => 4,
  }
}

function main(): i32 {
  return 0;
}
