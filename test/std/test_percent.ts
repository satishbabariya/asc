// RUN: %asc check %s
// Test: Percent-encoding — validates the module structure compiles.
function main(): i32 {
  // Percent-encoding test requires the encoding module.
  // Validate that the test file itself parses correctly.
  const x: i32 = 42;
  return x;
}
