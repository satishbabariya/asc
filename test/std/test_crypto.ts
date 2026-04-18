// RUN: %asc check %s
// Test: Crypto module (SHA-256, SHA-512, HMAC, subtle::eq, random/UUID) compiles.

function test_subtle_eq(): bool {
  // Equal byte buffers return true; different bytes/lengths return false.
  let a = [1, 2, 3, 4];
  let b = [1, 2, 3, 4];
  let c = [1, 2, 3, 5];
  let d = [1, 2, 3];
  let ok = subtle::eq(a, b);
  let diff = subtle::eq(a, c);
  let len_diff = subtle::eq(a, d);
  return ok && !diff && !len_diff;
}

function test_subtle_eq_str(): bool {
  let s1: String = String::new();
  s1.push_str("secret-token-abc");
  let s2: String = String::new();
  s2.push_str("secret-token-abc");
  let s3: String = String::new();
  s3.push_str("secret-token-xyz");
  return subtle::eq_str(s1, s2) && !subtle::eq_str(s1, s3);
}

function test_uuid_v4_bits(): bool {
  // uuid_v4 sets version nibble in byte 6 (0x40-0x4F) and variant in byte 8 (0x80-0xBF).
  let id = random::uuid_v4();
  let version_ok = (id[6] & 0xF0) == 0x40;
  let variant_ok = (id[8] & 0xC0) == 0x80;
  return version_ok && variant_ok;
}

function test_uuid_v4_string_format(): bool {
  // "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx" — 36 chars, 4 dashes, '4' at index 14, variant in [89ab] at index 19.
  let s = random::uuid_v4_string();
  if s.len() != 36 { return false; }
  let bytes = s.as_bytes();
  if bytes[8] != 0x2D { return false; }
  if bytes[13] != 0x2D { return false; }
  if bytes[18] != 0x2D { return false; }
  if bytes[23] != 0x2D { return false; }
  if bytes[14] != 0x34 { return false; }
  let v = bytes[19];
  let variant_ok = v == 0x38 || v == 0x39 || v == 0x61 || v == 0x62;
  return variant_ok;
}

function main(): i32 {
  if !test_subtle_eq() { return 1; }
  if !test_subtle_eq_str() { return 2; }
  if !test_uuid_v4_bits() { return 3; }
  if !test_uuid_v4_string_format() { return 4; }
  return 0;
}
