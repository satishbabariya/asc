// RUN: %asc check %s
trait Shape { fn area(ref<Self>): i32; }
struct Square { side: i32 }
struct Rect { w: i32, h: i32 }
struct Triangle { base: i32, height: i32 }
impl Shape for Square { fn area(ref<Square>): i32 { return self.side * self.side; } }
impl Shape for Rect { fn area(ref<Rect>): i32 { return self.w * self.h; } }
impl Shape for Triangle { fn area(ref<Triangle>): i32 { return self.base * self.height / 2; } }
function get_area(s: dyn Shape): i32 { return s.area(); }
function main(): i32 {
  let sq = Square { side: 5 }; let r = Rect { w: 3, h: 8 }; let t = Triangle { base: 6, height: 4 };
  let total: i32 = get_area(sq as dyn Shape) + get_area(r as dyn Shape) + get_area(t as dyn Shape);
  if total == 61 { return 0; } return 1;
}
