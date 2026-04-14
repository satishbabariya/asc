// RUN: %asc check %s
// Test: operator overloading via Add trait impl compiles.

struct Counter { val: i32 }

impl Add for Counter {
  function add(self: ref<Counter>, other: ref<Counter>): Counter {
    return Counter { val: self.val + other.val };
  }
}

function main(): i32 {
  let a = Counter { val: 10 };
  let b = Counter { val: 20 };
  return 0;
}
