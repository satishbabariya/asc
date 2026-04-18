// RUN: %asc check %s
// Test: bcrypt password hashing (RFC-0018 §2.3) — validates the module
// compiles and that hash/verify/cost round-trip through the public API.
// Runtime verification against reference vectors is deferred to an execution-
// capable harness (consistent with SHA-3 and Argon2 test posture).

function main(): i32 {
  // Round-trip smoke test: hash a known password at a low cost, then verify.
  let pw: str = "correct-horse-battery-staple";

  // Cost 4 keeps compile-time work modest; algorithm is identical to cost 12.
  let h = password::bcrypt_hash(pw, 4u8);
  assert!(h.is_ok());
  let hash_str = h.unwrap();
  assert_eq!(hash_str.len(), 60);

  // Verify against the produced hash — round-trip must succeed.
  let ok = password::bcrypt_verify(pw, hash_str.as_str());
  assert!(ok);

  // Wrong password must fail.
  let bad = password::bcrypt_verify("wrong", hash_str.as_str());
  assert!(!bad);

  // cost() echoes back the cost factor embedded in the hash.
  let c = password::bcrypt_cost(hash_str.as_str());
  assert!(c.is_some());
  assert_eq!(c.unwrap(), 4u8);

  // Default-cost wrapper also produces a 60-character hash.
  let hd = password::bcrypt_hash_default("hunter2");
  assert!(hd.is_ok());
  assert_eq!(hd.unwrap().len(), 60);

  // Boundary: cost < 4 rejected.
  let too_low = password::bcrypt_hash("x", 3u8);
  assert!(too_low.is_err());

  // Boundary: cost > 31 rejected.
  let too_high = password::bcrypt_hash("x", 32u8);
  assert!(too_high.is_err());

  // 72-byte truncation rule: > 72 bytes errors explicitly (not silent).
  let long_pw: str = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  let too_long = password::bcrypt_hash(long_pw, 4u8);
  assert!(too_long.is_err());

  // cost() on malformed input returns None.
  let bad_cost = password::bcrypt_cost("not-a-hash");
  assert!(bad_cost.is_none());

  return 0;
}
