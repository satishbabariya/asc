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
