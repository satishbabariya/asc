// RUN: %asc build %s --emit llvmir > %t.out 2>&1; grep -q "__asc_panic" %t.out
// Test: panic function declared (double-panic runtime behavior).

function may_panic(): void {
  panic!("test panic");
}

function main(): i32 {
  return 0;
}
