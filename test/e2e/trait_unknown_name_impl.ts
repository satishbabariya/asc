// RUN: not %asc check %s 2>&1 | FileCheck %s
// CHECK: unknown trait
// Test: `impl <UnknownTrait> for T` is rejected.

struct Foo { v: i32 }

impl BogusTraitThatDoesNotExist for Foo {
  fn whatever(self: ref<Self>): i32 { return 0; }
}

function main(): i32 { return 0; }
