// RUN: %asc check %s
// Test: @copy struct passed by value to function.

@copy
struct Point { x: i32, y: i32 }

function add_points(a: Point, b: Point): Point {
  return Point { x: a.x + b.x, y: a.y + b.y };
}

function main(): i32 {
  let p1 = Point { x: 10, y: 20 };
  let p2 = Point { x: 5, y: 7 };
  let result = add_points(p1, p2);
  return result.x + result.y;
}
