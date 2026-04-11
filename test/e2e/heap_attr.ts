// RUN: %asc build %s --emit llvmir > %t.out 2>&1; grep -q "malloc" %t.out
// Tests that @heap attribute forces heap allocation via malloc.

struct Data { value: i32 }

function main(): i32 {
  @heap let d = Data { value: 42 };
  return d.value;
}
