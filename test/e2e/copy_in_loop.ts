// Test: @copy struct passed through function in a loop.

@copy
struct Counter { val: i32 }

function increment(c: Counter): Counter {
  return Counter { val: c.val + 1 };
}

function main(): i32 {
  let c = Counter { val: 0 };
  let i: i32 = 0;
  while i < 42 {
    c = increment(c);
    i = i + 1;
  }
  return c.val;
}
