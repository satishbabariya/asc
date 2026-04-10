// RUN: %asc check %s
// Test: enum with match-as-expression inside function.

enum Direction {
  North,
  South,
  East,
  West,
}

function opposite(d: Direction): i32 {
  return match d {
    0 => 1,
    1 => 0,
    2 => 3,
    _ => 2,
  };
}

function main(): i32 {
  let n = Direction::North;
  let s = Direction::South;
  return opposite(n) + opposite(s);
}
