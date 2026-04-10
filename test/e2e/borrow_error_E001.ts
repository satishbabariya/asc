// RUN: %asc check %s
// Tests that sequential borrows (non-overlapping) are allowed.
// read(&d) finishes before write(&d) starts — no conflict.

struct Data { value: i32 }

function read(d: ref<Data>): i32 { return d.value; }
function write(d: refmut<Data>): void { d.value = 1; }

function main(): void {
  let d = Data { value: 0 };
  const v = read(&d);
  write(&d);
}
