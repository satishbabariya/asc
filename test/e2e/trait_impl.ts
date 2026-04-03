// Test trait and impl blocks.

trait Describable {
  fn describe(ref<Self>): i32;
}

struct Circle {
  radius: f64,
}

impl Circle {
  fn new(r: f64): Circle {
    return Circle { radius: r };
  }
}

impl Describable for Circle {
  fn describe(ref<Self>): i32 {
    return 1;
  }
}

function main(): i32 {
  const c = Circle::new(5.0);
  return 0;
}
