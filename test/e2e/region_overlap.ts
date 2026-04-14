// RUN: %asc check %s > %t.out 2>&1; grep -q "E001" %t.out
// Test: region-annotated borrows — conflicting borrows still detected.

struct Counter { n: i32 }

function use_both(a: ref<Counter>, b: refmut<Counter>): void {
  b.n = a.n;
}

function main(): void {
  let c = Counter { n: 0 };
  use_both(c, c);
}
