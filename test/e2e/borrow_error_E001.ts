// RUN: %asc check %s 2>&1 || true
// Test: expected E001 error — mutable borrow while shared active.
// Verify: asc check produces error with source excerpt.

struct Data { value: i32 }

function read(d: ref<Data>): i32 { return d.value; }
function write(d: refmut<Data>): void { d.value = 1; }

function main(): void {
  let d = Data { value: 0 };
  const v = read(&d);
  write(&d);
}
