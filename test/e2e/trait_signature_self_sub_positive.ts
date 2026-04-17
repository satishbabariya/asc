// RUN: %asc check %s
// Test: Self-substitution accepts impl that returns own<ConcreteType>.
// Registered Clone trait in Builtins.cpp: fn clone(ref<Self>): Self
// After Self -> Foo: expected return is Foo (bare), matching `return Foo { ... }`.
// This validates the common Clone idiom used throughout std/.

struct Foo { v: i32 }

impl Clone for Foo {
  fn clone(self: ref<Self>): Foo {
    return Foo { v: self.v };
  }
}

function main(): i32 { return 0; }
