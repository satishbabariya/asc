// RUN: %asc check %s > %t.out 2>&1; grep -q "E004" %t.out
// Expected: E004 error — use of value after move.

struct Buffer {
  size: usize,
}

function consume(buf: own<Buffer>): void { }

function main(): void {
  const buf = Buffer { size: 1024 };
  consume(buf);
  consume(buf);
}
