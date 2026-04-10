// RUN: %asc check %s
// Tests that sequential borrows with no drop-while-borrowed are allowed.
// E002/E003 would fire if a drop happened while a borrow was still active.

struct Data { value: i32 }

function read(d: ref<Data>): i32 { return d.value; }

function main(): i32 {
  let d = Data { value: 42 };
  let v = read(d);
  return v;
}
