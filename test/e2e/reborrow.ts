// Test: mutable borrow passed to function multiple times (reborrowing).

struct Counter { value: i32 }

function increment(c: refmut<Counter>): void {
  c.value = c.value + 1;
}

function main(): i32 {
  let c = Counter { value: 0 };
  increment(&c);
  increment(&c);
  increment(&c);
  return c.value;
}
