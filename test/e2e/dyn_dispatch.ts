// RUN: %asc check %s
// Test: dynamic trait dispatch via dyn Trait fat pointer.

trait Speak {
  fn sound(ref<Self>): i32;
}

struct Dog {}
struct Cat {}

impl Speak for Dog {
  fn sound(ref<Dog>): i32 { return 1; }
}

impl Speak for Cat {
  fn sound(ref<Cat>): i32 { return 2; }
}

function call_sound(s: dyn Speak): i32 {
  return s.sound();
}

function main(): i32 {
  let d = Dog {};
  let c = Cat {};
  let a: i32 = call_sound(d as dyn Speak);
  let b: i32 = call_sound(c as dyn Speak);
  return a + b;
}
