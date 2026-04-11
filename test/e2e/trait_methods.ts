// RUN: %asc check %s

trait Printable {
  function display(self: ref<Self>): i32;
}

struct Point { x: i32, y: i32 }

impl Printable for Point {
  function display(self: ref<Point>): i32 {
    return self.x + self.y;
  }
}

function main(): i32 {
  let p = Point { x: 3, y: 4 };
  return 0;
}
