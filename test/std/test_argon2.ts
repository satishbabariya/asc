// RUN: %asc check %s
// Test: Argon2id password hashing (RFC 9106) — validates the test file
// compiles. The implementation lives in std/crypto/argon2.ts; because the
// module loader is not wired for std files yet (see test_sha256/sha512/crypto
// precedent), this test only validates the RUN line and parser integration.
// Round-trip hash/verify coverage is tracked in the argon2 source doc-tests.
function main(): i32 {
  // Sanity marker: 0x13 is the Argon2 version constant (§3.1 of RFC 9106).
  const version: u32 = 0x13;
  if version != 19 { return 1; }
  return 0;
}
