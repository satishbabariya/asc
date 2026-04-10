// RUN: %asc check %s
// Test: static dispatch on trait still works with vtable generation.

trait HasValue {
  fn get_value(ref<Self>): i32;
}

struct MyVal { v: i32 }

impl HasValue for MyVal {
  fn get_value(ref<MyVal>): i32 { return self.v; }
}

function main(): i32 {
  let x = MyVal { v: 42 };
  return x.get_value();
}
