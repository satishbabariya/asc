// RUN: %asc check %s 2>&1 | grep -c "note:" > %t.count; test $(cat %t.count) -ge 2
// Test: E001 diagnostic includes multiple notes showing borrow provenance.

struct Data { value: i32 }

function read(d: ref<Data>): i32 { return d.value; }
function write(d: refmut<Data>): void { d.value = 1; }

function main(): void {
  let d = Data { value: 42 };
  use_both(d, d);
}

function use_both(a: ref<Data>, b: refmut<Data>): void {
  b.value = a.value;
}
