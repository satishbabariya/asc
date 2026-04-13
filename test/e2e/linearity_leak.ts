// RUN: %asc check %s > %t.out 2>&1; grep -q "W004" %t.out
// Test: owned struct value created but never used should produce W004.

struct Leaky { data: i32 }

function main(): i32 {
  const leaked = Leaky { data: 99 };
  return 0;
}
