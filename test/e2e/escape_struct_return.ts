// RUN: %asc build %s --emit llvmir > %t.out 2>&1
// RUN: grep -q "call.*@malloc" %t.out
// Test: struct literal returned from function should be heap-allocated via escape analysis.

struct Point { x: i32, y: i32 }
function make_point(): own<Point> {
  let p = Point { x: 1, y: 2 };
  return p;
}
function main(): i32 {
  let p = make_point();
  return 0;
}
