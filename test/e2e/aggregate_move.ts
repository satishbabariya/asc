// Tests that struct values can be passed to functions (aggregate ownership transfer).
// RUN: %asc build %s --emit mlir > %t.out 2>&1; grep -q "take_point" %t.out

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
