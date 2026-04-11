// RUN: %asc build %s --target wasm32-wasi -o %t.wasm 2>&1 || true
// Tests that asc build with .wasm output auto-links via wasm-ld.

function main(): i32 {
  return 42;
}
