// RUN: %asc check %s
// Tests borrow ops are emitted for ref/refmut parameters.
// TODO: borrow checker needs SSA alias analysis to detect same-variable conflicts.

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
