// RUN: %asc check %s

@copy
struct Vec2 { x: i32, y: i32 }

function double_vec(v: Vec2): Vec2 {
  return Vec2 { x: v.x * 2, y: v.y * 2 };
}

function main(): i32 {
  let a = Vec2 { x: 1, y: 2 };
  let b = double_vec(a);
  let c = a.x + b.x;
  return c;
}
