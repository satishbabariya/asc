// RUN: %asc check %s > %t.out 2>&1; grep -q "W003\|exhaustive" %t.out
// Test: incomplete nested pattern match produces warning.

enum Color { Red, Blue }

function describe(c: Color): i32 {
  match c {
    Color::Red => 1,
  }
}

function main(): i32 { return 0; }
