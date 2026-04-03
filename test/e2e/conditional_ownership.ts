// Test: conditional return with struct field access.

struct Item { value: i32 }

function choose(flag: bool): i32 {
  let a = Item { value: 42 };
  let b = Item { value: 10 };
  if flag {
    return a.value;
  }
  return b.value;
}

function main(): i32 {
  return choose(true);
}
