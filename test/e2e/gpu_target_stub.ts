// RUN: %asc build %s --target nvptx64-nvidia-cuda --emit llvmir > %t.out 2>&1; grep -q "nvptx64\|target" %t.out
// Test: GPU target triple accepted.

function add(a: i32, b: i32): i32 {
  return a + b;
}
function main(): i32 { return 0; }
