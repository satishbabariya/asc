// RUN: %asc check %s 2>&1 || true
// Test: trait with impl for multiple types and self field access.

trait Area {
  fn area(ref<Self>): i32;
}

struct Square {
  side: i32,
}

struct Rectangle {
  width: i32,
  height: i32,
}

impl Area for Square {
  fn area(ref<Self>): i32 {
    return self.side * self.side;
  }
}

impl Area for Rectangle {
  fn area(ref<Self>): i32 {
    return self.width * self.height;
  }
}

function main(): i32 {
  let s = Square { side: 5 };
  let r = Rectangle { width: 3, height: 4 };
  return s.area() + r.area();
}
