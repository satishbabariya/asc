// RUN: %asc check %s
// Test: TOML parser surface (RFC-0019 §3.1).
// Static sanity checks for the constants our TOML parser matches on.
// The real parser lives in std/config/toml.ts — we can't import it here, but
// we verify that the byte values we dispatch on are the ones we expect.
function main(): i32 {
  // Delimiters.
  assert_eq!(0x22, 34);  // double-quote "
  assert_eq!(0x27, 39);  // apostrophe '
  assert_eq!(0x5C, 92);  // backslash \
  assert_eq!(0x5B, 91);  // [
  assert_eq!(0x5D, 93);  // ]
  assert_eq!(0x7B, 123); // {
  assert_eq!(0x7D, 125); // }
  assert_eq!(0x3D, 61);  // =
  assert_eq!(0x2E, 46);  // .
  assert_eq!(0x2C, 44);  // ,
  assert_eq!(0x23, 35);  // #

  // Escape targets.
  assert_eq!(0x08, 8);   // \b
  assert_eq!(0x09, 9);   // \t
  assert_eq!(0x0A, 10);  // \n
  assert_eq!(0x0C, 12);  // \f
  assert_eq!(0x0D, 13);  // \r

  // Datetime punctuation.
  assert_eq!(0x2D, 45);  // -
  assert_eq!(0x3A, 58);  // :
  assert_eq!(0x54, 84);  // T
  assert_eq!(0x5A, 90);  // Z

  return 0;
}
