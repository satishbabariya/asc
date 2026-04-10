// RUN: %asc check %s
// Tests borrow ops for ref/refmut parameters with &d syntax.
// TODO: E001 detection needs SSA alias analysis for same-variable tracking.

struct Data { value: i32 }

function read(d: ref<Data>): i32 { return d.value; }
function write(d: refmut<Data>): void { d.value = 1; }

function main(): void {
  let d = Data { value: 0 };
  const v = read(&d);
  write(&d);
}
