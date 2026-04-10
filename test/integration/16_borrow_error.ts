// RUN: %asc check %s > %t.out 2>&1; grep -q "error" %t.out
// test 16: borrow error detection — should fail with E001
struct Data {
  value: i32,
}

function consume(d: own<Data>): void { }

function main(): void {
  let d = Data { value: 0 };
  consume(d);
  consume(d);
}
