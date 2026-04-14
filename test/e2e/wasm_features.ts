// RUN: %asc build %s --target wasm32-wasi --emit llvmir > %t.out 2>&1; test $? -eq 0
// Test: wasm target compiles successfully.

function main(): i32 {
  return 42;
}
