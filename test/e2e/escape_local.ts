// RUN: %asc check %s
// Test: purely local value — no escape, no errors.

struct Local { x: i32 }

function main(): i32 {
  let loc = Local { x: 5 };
  return 0;
}
