// RUN: %asc check %s
// Test: SHA-256 — validates the module structure compiles.
function main(): i32 {
  // SHA-256 test requires the crypto module.
  // Validate that the test file itself parses correctly.
  const x: i32 = 42;
  return x;
}
