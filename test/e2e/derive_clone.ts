// RUN: %asc check %s
// Test: @derive(Clone) generates callable clone method.

@derive(Clone)
struct Point { x: i32, y: i32 }

function main(): i32 {
  let p = Point { x: 10, y: 20 };
  let q = p.clone();
  return 0;
}
