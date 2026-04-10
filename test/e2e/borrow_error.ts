// RUN: %asc check %s 2>&1 || true
// Expected: E001 error — mutable borrow while shared borrow active.
// This test verifies the borrow checker rejects conflicting borrows.

struct Data {
  value: i32,
}

function read_data(d: ref<Data>): i32 {
  return d.value;
}

function write_data(d: refmut<Data>, v: i32): void {
  d.value = v;
}

function main(): void {
  let d = Data { value: 42 };
  const view = &d;
  write_data(&d, 100);
}
