// RUN: %asc check %s > %t.out 2>&1; grep -q "E001" %t.out
// Tests that simultaneous shared + mutable borrows trigger E001.

struct Data {
  value: i32,
}

function use_both(a: ref<Data>, b: refmut<Data>): void {
  b.value = a.value;
}

function main(): void {
  let d = Data { value: 42 };
  use_both(d, d);
}
