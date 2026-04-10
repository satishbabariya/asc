// RUN: %asc check %s
// Test: simple function that returns 42 — Wasm EH stub.

function main(): i32 {
  return 42;
}
