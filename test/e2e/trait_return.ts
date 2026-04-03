trait Measurable { fn measure(ref<Self>): i32; }
struct Line { length: i32 }
struct Circle { radius: i32 }
impl Measurable for Line { fn measure(ref<Line>): i32 { return self.length; } }
impl Measurable for Circle { fn measure(ref<Circle>): i32 { return self.radius * 2; } }
function total_measure(a: dyn Measurable, b: dyn Measurable): i32 { return a.measure() + b.measure(); }
function main(): i32 {
  let l = Line { length: 15 }; let c = Circle { radius: 7 };
  return total_measure(l as dyn Measurable, c as dyn Measurable);
}
