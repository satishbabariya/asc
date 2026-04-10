// Tests that struct moves emit memcpy for aggregate types.
// RUN: %asc build %s --emit llvmir -o - | grep -q "llvm.memcpy"

struct Point {
  x: i32,
  y: i32,
}

function take_point(p: own<Point>): i32 {
  return p.x;
}

function main(): i32 {
  let p = Point { x: 10, y: 20 };
  let result = take_point(p);
  return result;
}
