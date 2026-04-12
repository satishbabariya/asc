// RUN: %asc check %s

trait Add {
  function add(self: ref<Self>, rhs: ref<Self>): Self;
}

struct Vec2 { x: i32, y: i32 }

impl Add for Vec2 {
  function add(self: ref<Vec2>, rhs: ref<Vec2>): Vec2 {
    return Vec2 { x: self.x + rhs.x, y: self.y + rhs.y };
  }
}

function main(): i32 {
  return 0;
}
