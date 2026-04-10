// RUN: %asc check %s > %t.out 2>&1; grep -q "unsupported" %t.out
// Expected: error about unsupported 'class' keyword.

class Foo {
  x: i32;
}

function main(): i32 {
  return 0;
}
