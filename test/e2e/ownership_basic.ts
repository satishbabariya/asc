// RUN: %asc check %s
// Test basic ownership transfer.
// consume() takes an own<Buffer>, so buf is moved.

struct Buffer {
  size: usize,
}

function consume(data: own<Buffer>): void { }

function main(): void {
  const buf = Buffer { size: 1024 };
  consume(buf);
}
