// Test: borrow used within scope — no dangling reference.

struct Data { value: i32 }

function read_ref(d: ref<Data>): i32 {
  return d.value;
}

function main(): i32 {
  let d = Data { value: 42 };
  let result = read_ref(&d);
  return result;
}
