// RUN: %asc check %s
// Test: struct field access on non-copy struct — no move, just field read.

struct Data { x: i32, y: i32 }

function main(): i32 {
  let d = Data { x: 10, y: 32 };
  return d.x + d.y;
}
