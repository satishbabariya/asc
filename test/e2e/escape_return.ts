// RUN: %asc build %s --emit llvmir > %t.out 2>&1; grep -q "malloc" %t.out
// Test: returned value auto-promoted to heap by escape analysis.

struct Data { value: i32 }

function make_data(): own<Data> {
  let d = Data { value: 99 };
  return d;
}

function main(): i32 {
  return 0;
}
