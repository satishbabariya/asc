// Test: expected E004 error — use of value after move.

struct Resource { id: i32 }

function consume(r: own<Resource>): void { }

function main(): void {
  const r = Resource { id: 1 };
  consume(r);
  consume(r);
}
