// RUN: %asc check %s > %t.out 2>&1; grep -q "E004" %t.out
// test 17: use-after-move detection — should fail with E004
struct Resource {
  id: i32,
}

function consume(r: own<Resource>): void { }

function main(): void {
  let r = Resource { id: 1 };
  consume(r);
  consume(r);
}
