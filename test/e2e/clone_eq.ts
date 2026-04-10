// RUN: %asc check %s
// Test: .clone() and struct == comparison.

struct Point { x: i32, y: i32 }

function main(): i32 {
  let a = Point { x: 5, y: 10 };
  let b = a.clone();
  let equal: i32 = 0;
  if a == b {
    equal = 1;
  }
  return b.x + b.y + equal;
}
