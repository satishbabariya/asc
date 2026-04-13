// RUN: %asc check %s > %t.out 2>&1; grep -qE "E004|E006" %t.out
// Test: two calls consuming the same owned value — E004 from Sema or E006 from linearity.

struct Resource { id: i32 }

function consume(r: own<Resource>): void { }

function main(): void {
  const r = Resource { id: 1 };
  consume(r);
  consume(r);
}
