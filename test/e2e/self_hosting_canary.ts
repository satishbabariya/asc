// RUN: %asc check %s
// Test: self-hosting canary — exercises structs, traits, closures, patterns, generics.

struct Counter { val: i32 }

impl Counter {
  function get(self: ref<Counter>): i32 {
    return self.val;
  }
}

function apply(f: (i32) => i32, x: i32): i32 {
  return f(x);
}

struct Wrapper<T> { value: T }

function main(): i32 {
  let c = Counter { val: 42 };
  let v = c.get();
  let doubled = apply((x: i32): i32 => { return x * 2; }, v);

  let w = Wrapper<i32> { value: doubled };

  let result: i32 = 0;
  match doubled {
    84 => { result = 1; },
    _ => { result = 0; },
  }

  return result;
}
