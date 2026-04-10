// RUN: %asc check %s
// Test: trait with method impl — basic named trait dispatch.

trait Named {
  fn get_id(ref<Self>): i32;
}

struct Widget { id: i32 }

impl Named for Widget {
  fn get_id(ref<Widget>): i32 {
    return self.id;
  }
}

function main(): i32 {
  let w = Widget { id: 42 };
  return w.get_id();
}
