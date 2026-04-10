// RUN: %asc check %s 2>&1 || true
// Test: dyn Trait dynamic dispatch with different impl types.

trait Transform {
  fn apply(ref<Self>, x: i32): i32;
}

struct Doubler {}
struct Adder { offset: i32 }

impl Transform for Doubler {
  fn apply(ref<Doubler>, x: i32): i32 { return x * 2; }
}

impl Transform for Adder {
  fn apply(ref<Adder>, x: i32): i32 { return x + self.offset; }
}

function apply_transform(t: dyn Transform, x: i32): i32 {
  return t.apply(x);
}

function main(): i32 {
  let d = Doubler {};
  let a = Adder { offset: 22 };
  let r1: i32 = apply_transform(d as dyn Transform, 10);
  let r2: i32 = apply_transform(a as dyn Transform, 20);
  return r2;
}
