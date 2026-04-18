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

/// RFC-0018 §2.4 constant-time byte slice equality (alias of constant_time_eq).
/// False for different lengths, still constant-time over the shorter prefix.
function eq(a: ref<[u8]>, b: ref<[u8]>): bool {
  let la = a.len();
  let lb = b.len();
  let n: usize = 0;
  if la < lb { n = la; } else { n = lb; }
  let diff: u8 = 0;
  let i: usize = 0;
  while i < n {
    diff = diff | (a[i] ^ b[i]);
    i = i + 1;
  }
  // Fold length mismatch into the accumulator so both comparisons
  // execute regardless of where the mismatch is. Works for any usize width.
  let lx = la ^ lb;
  let fold: usize = lx | (lx >> 32);
  let len_diff: u8 = (fold as u8) | ((fold >> 8) as u8) | ((fold >> 16) as u8) | ((fold >> 24) as u8);
  return (diff | len_diff) == 0;
}

/// RFC-0018 §2.4 constant-time string equality via byte view.
function eq_str(a: ref<String>, b: ref<String>): bool {
  return eq(a.as_bytes(), b.as_bytes());
}
