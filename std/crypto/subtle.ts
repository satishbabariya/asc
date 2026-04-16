// std/crypto/subtle.ts — Constant-time comparison (RFC-0018)

/// Constant-time byte slice equality comparison.
/// Always takes the same time regardless of where the first mismatch occurs.
/// Both slices must have the same length; returns false if lengths differ.
function constant_time_eq(a: ref<[u8]>, b: ref<[u8]>): bool {
  if a.len() != b.len() { return false; }
  let diff: u8 = 0;
  let i: usize = 0;
  while i < a.len() {
    diff = diff | (a[i] ^ b[i]);
    i = i + 1;
  }
  return diff == 0;
}
