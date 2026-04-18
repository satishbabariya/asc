// RUN: %asc check %s > %t.out 2>&1; grep -q "no static method" %t.out
// RUN: %asc check %s > %t.out 2>&1; grep -q "no_such" %t.out
// Test: calling a nonexistent static method on a type with impls is rejected.
// Previously checkPathExpr fell through silently when Type::method didn't
// match any impl, leaving the caller to guess what the path resolved to.

struct Widget { n: i32 }

impl Widget {
  fn new(n: i32): own<Widget> {
    return Widget { n };
  }
}

function main(): i32 {
  let w = Widget::no_such_ctor(3);
  return 0;
}
