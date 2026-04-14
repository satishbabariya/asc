// RUN: %asc check %s > %t.out 2>&1; grep -q "E007\|E002\|borrow" %t.out
// Test: E007 — borrow of local value escapes function scope.
// The borrow checker should detect that a borrow of a local is returned,
// which violates the outlives constraint. If Sema catches this first
// (e.g., as E002), that is also acceptable.

struct Data { value: i32 }

function get_ref(): ref<Data> {
  let d = Data { value: 42 };
  return &d;
}

function main(): i32 {
  return 0;
}
