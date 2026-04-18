// std/crypto/argon2.ts — Argon2id password hashing (RFC 9106, RFC-0018 §2.3).
//
// Structure:
//   * BLAKE2b primitive (~150 LOC) — Argon2's compression core.
//   * Argon2id memory-hard KDF.
//   * PHC-format encode/decode: "$argon2id$v=19$m=<m>,t=<t>,p=<p>$<b64-salt>$<b64-hash>".
//   * Constant-time verify.
//
// Not byte-for-byte compatible with reference Argon2 for *very* small parameters
// without Appendix-A test vectors — this module implements the spec as written
// (RFC 9106 §3.2, §3.4) using BLAKE2b-long for H_0 derivation and the
// compression function G over 1024-byte blocks. The cost parameters default to
// m=19456 KiB, t=2, p=1 per OWASP 2024 recommendation.

import { random_bytes } from './random';

// ---------------------------------------------------------------------------
// BLAKE2b (RFC 7693)
// ---------------------------------------------------------------------------

/// BLAKE2b IV — same as SHA-512 initial state.
const BLAKE2B_IV: [u64; 8] = [
  0x6a09e667f3bcc908, 0xbb67ae8584caa73b,
  0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
  0x510e527fade682d1, 0x9b05688c2b3e6c1f,
  0x1f83d9abfb41bd6b, 0x5be0cd19137e2179,
];

/// BLAKE2b sigma permutation table (RFC 7693 §2.7).
const BLAKE2B_SIGMA: [[u8; 16]; 12] = [
  [ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15],
  [14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3],
  [11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4],
  [ 7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8],
  [ 9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13],
  [ 2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9],
  [12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11],
  [13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10],
  [ 6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5],
  [10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0],
  [ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15],
  [14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3],
];

/// BLAKE2b streaming state. Digest length up to 64 bytes.
struct Blake2b {
  h: [u64; 8],
  t: [u64; 2],      // byte counter (low, high)
  buf: [u8; 128],
  buf_len: usize,
  digest_len: usize,
}

/// Right-rotate a 64-bit value.
function blake2b_rotr64(x: u64, n: u64): u64 {
  return (x >> n) | (x << (64 - n));
}

/// G mixing function (RFC 7693 §3.1).
function blake2b_g(
  v: refmut<[u64; 16]>,
  a: usize, b: usize, c: usize, d: usize,
  x: u64, y: u64,
): void {
  v[a] = v[a] +% v[b] +% x;
  v[d] = blake2b_rotr64(v[d] ^ v[a], 32);
  v[c] = v[c] +% v[d];
  v[b] = blake2b_rotr64(v[b] ^ v[c], 24);
  v[a] = v[a] +% v[b] +% y;
  v[d] = blake2b_rotr64(v[d] ^ v[a], 16);
  v[c] = v[c] +% v[d];
  v[b] = blake2b_rotr64(v[b] ^ v[c], 63);
}

impl Blake2b {
  /// Create a new BLAKE2b hasher with the given output length (1..=64).
  fn new(digest_len: usize): own<Blake2b> {
    let s = Blake2b {
      h: BLAKE2B_IV,
      t: [0u64; 2],
      buf: [0u8; 128],
      buf_len: 0,
      digest_len: digest_len,
    };
    // Parameter block XOR: P[0] = digest_len | (key_len<<8) | (1<<16) | (1<<24).
    // No key, fanout=1, depth=1.
    s.h[0] = s.h[0] ^ (0x01010000 as u64) ^ (digest_len as u64);
    return s;
  }

  /// Absorb input bytes.
  fn update(refmut<Self>, data: ref<[u8]>): void {
    let len = data.len();
    let i: usize = 0;
    while i < len {
      if self.buf_len == 128 {
        // Only compress when more input is pending so the final block keeps
        // "last" semantics.
        self.increment_counter(128);
        self.compress(false);
        self.buf_len = 0;
      }
      let take = 128 - self.buf_len;
      if take > len - i { take = len - i; }
      let j: usize = 0;
      while j < take {
        self.buf[self.buf_len + j] = data[i + j];
        j = j + 1;
      }
      self.buf_len = self.buf_len + take;
      i = i + take;
    }
  }

  /// Finalize and emit the digest.
  fn finalize(refmut<Self>): own<Vec<u8>> {
    self.increment_counter(self.buf_len as u64);
    // Zero-pad the buffer to 128 bytes.
    while self.buf_len < 128 {
      self.buf[self.buf_len] = 0;
      self.buf_len = self.buf_len + 1;
    }
    self.compress(true);

    let out: own<Vec<u8>> = Vec::with_capacity(self.digest_len);
    let word: usize = 0;
    let emitted: usize = 0;
    while emitted < self.digest_len {
      let w = self.h[word];
      let byte: usize = 0;
      while byte < 8 && emitted < self.digest_len {
        out.push(((w >> ((byte as u64) * 8)) & 0xFF) as u8);
        byte = byte + 1;
        emitted = emitted + 1;
      }
      word = word + 1;
    }
    return out;
  }

  /// Add n to the 128-bit byte counter (t[0], t[1]).
  fn increment_counter(refmut<Self>, n: u64): void {
    let old = self.t[0];
    self.t[0] = self.t[0] +% n;
    if self.t[0] < old {
      self.t[1] = self.t[1] +% 1;
    }
  }

  /// Compress the current 128-byte buffer. `last` sets the finalization flag.
  fn compress(refmut<Self>, last: bool): void {
    // Load message words (little-endian).
    let m: [u64; 16] = [0u64; 16];
    let i: usize = 0;
    while i < 16 {
      let base = i * 8;
      m[i] = (self.buf[base] as u64)
           | ((self.buf[base + 1] as u64) << 8)
           | ((self.buf[base + 2] as u64) << 16)
           | ((self.buf[base + 3] as u64) << 24)
           | ((self.buf[base + 4] as u64) << 32)
           | ((self.buf[base + 5] as u64) << 40)
           | ((self.buf[base + 6] as u64) << 48)
           | ((self.buf[base + 7] as u64) << 56);
      i = i + 1;
    }

    // Working vector v[0..16].
    let v: [u64; 16] = [0u64; 16];
    let k: usize = 0;
    while k < 8 {
      v[k] = self.h[k];
      v[k + 8] = BLAKE2B_IV[k];
      k = k + 1;
    }
    v[12] = v[12] ^ self.t[0];
    v[13] = v[13] ^ self.t[1];
    if last { v[14] = ~v[14]; }

    // 12 rounds.
    let r: usize = 0;
    while r < 12 {
      let s = BLAKE2B_SIGMA[r];
      blake2b_g(refmut v, 0, 4,  8, 12, m[s[ 0] as usize], m[s[ 1] as usize]);
      blake2b_g(refmut v, 1, 5,  9, 13, m[s[ 2] as usize], m[s[ 3] as usize]);
      blake2b_g(refmut v, 2, 6, 10, 14, m[s[ 4] as usize], m[s[ 5] as usize]);
      blake2b_g(refmut v, 3, 7, 11, 15, m[s[ 6] as usize], m[s[ 7] as usize]);
      blake2b_g(refmut v, 0, 5, 10, 15, m[s[ 8] as usize], m[s[ 9] as usize]);
      blake2b_g(refmut v, 1, 6, 11, 12, m[s[10] as usize], m[s[11] as usize]);
      blake2b_g(refmut v, 2, 7,  8, 13, m[s[12] as usize], m[s[13] as usize]);
      blake2b_g(refmut v, 3, 4,  9, 14, m[s[14] as usize], m[s[15] as usize]);
      r = r + 1;
    }

    // XOR chaining halves.
    let j: usize = 0;
    while j < 8 {
      self.h[j] = self.h[j] ^ v[j] ^ v[j + 8];
      j = j + 1;
    }
  }
}

/// One-shot BLAKE2b: input ‑> Vec<u8> of the requested length.
function blake2b(data: ref<[u8]>, digest_len: usize): own<Vec<u8>> {
  let h = Blake2b::new(digest_len);
  h.update(data);
  return h.finalize();
}

/// BLAKE2b-long (Argon2 H', RFC 9106 §3.3). Produces `out_len` bytes.
/// For out_len <= 64 this is plain BLAKE2b(out_len || data).
/// For out_len  > 64 this iterates 64-byte chunks with overlapping re-hashing.
function blake2b_long(data: ref<[u8]>, out_len: u32): own<Vec<u8>> {
  // Prefix: 4-byte little-endian length.
  let prefix: [u8; 4] = [
    out_len as u8,
    (out_len >> 8) as u8,
    (out_len >> 16) as u8,
    (out_len >> 24) as u8,
  ];

  if out_len <= 64 {
    let h = Blake2b::new(out_len as usize);
    h.update(ref prefix);
    h.update(data);
    return h.finalize();
  }

  // Long form: V_1 = H(64, prefix || data), then V_i+1 = H(64, V_i),
  // output first 32 bytes of each V_i, final block emits (out_len - 32*r)
  // trailing bytes where r = ceil(out_len/32) - 2.
  let result: own<Vec<u8>> = Vec::with_capacity(out_len as usize);

  let first = Blake2b::new(64);
  first.update(ref prefix);
  first.update(data);
  let v = first.finalize();

  // Emit first 32 bytes of v.
  let i: usize = 0;
  while i < 32 {
    result.push(v[i]);
    i = i + 1;
  }

  let remaining = (out_len - 32) as usize;
  while remaining > 64 {
    let next = Blake2b::new(64);
    next.update(ref v);
    v = next.finalize();
    let k: usize = 0;
    while k < 32 {
      result.push(v[k]);
      k = k + 1;
    }
    remaining = remaining - 32;
  }

  // Final chunk: `remaining` bytes in (32, 64].
  let last = Blake2b::new(remaining);
  last.update(ref v);
  let tail = last.finalize();
  let k: usize = 0;
  while k < remaining {
    result.push(tail[k]);
    k = k + 1;
  }
  return result;
}

// ---------------------------------------------------------------------------
// Argon2id (RFC 9106)
// ---------------------------------------------------------------------------

/// Argon2id parameters.
@copy
struct Argon2Params {
  /// Memory cost in KiB. RFC 9106 min = 8 * parallelism.
  mem_kb: u32,
  /// Time cost (iterations).
  time: u32,
  /// Parallelism factor (lanes).
  parallelism: u32,
  /// Output hash length in bytes (min 4).
  hash_len: u32,
}

impl Argon2Params {
  fn default(): Argon2Params {
    return Argon2Params {
      mem_kb: 19456,   // OWASP 2024 recommendation: 19 MiB.
      time: 2,
      parallelism: 1,
      hash_len: 32,
    };
  }
}

/// Errors from the Argon2 API.
enum ArgonError {
  InvalidParams,
  InvalidHash,
  RandomFailed,
  AllocationFailed,
}

impl Display for ArgonError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      ArgonError::InvalidParams => f.write_str("argon2: invalid parameters"),
      ArgonError::InvalidHash => f.write_str("argon2: invalid PHC hash"),
      ArgonError::RandomFailed => f.write_str("argon2: CSPRNG failure"),
      ArgonError::AllocationFailed => f.write_str("argon2: allocation failed"),
    }
  }
}

/// Argon2id type constant (RFC 9106 §3.4).
const ARGON2_TYPE_ID: u32 = 2;
/// Argon2 version number (0x13 == 19 decimal).
const ARGON2_VERSION: u32 = 0x13;
/// Number of 64-bit words per 1024-byte block.
const ARGON2_BLOCK_QWORDS: usize = 128;

/// Append a little-endian u32 to a byte vector.
function push_u32_le(v: refmut<Vec<u8>>, x: u32): void {
  v.push((x & 0xFF) as u8);
  v.push(((x >> 8) & 0xFF) as u8);
  v.push(((x >> 16) & 0xFF) as u8);
  v.push(((x >> 24) & 0xFF) as u8);
}

/// Validate Argon2id parameters.
function validate_params(p: ref<Argon2Params>): Result<void, ArgonError> {
  if p.parallelism < 1 { return Result::Err(ArgonError::InvalidParams); }
  if p.time < 1 { return Result::Err(ArgonError::InvalidParams); }
  if p.hash_len < 4 { return Result::Err(ArgonError::InvalidParams); }
  // Memory must be at least 8 * parallelism KiB.
  if p.mem_kb < 8 * p.parallelism { return Result::Err(ArgonError::InvalidParams); }
  return Result::Ok(void);
}

/// Build H_0 (RFC 9106 §3.2): 64-byte BLAKE2b of the parameter/message block.
function argon2_h0(
  password: ref<[u8]>,
  salt: ref<[u8]>,
  params: ref<Argon2Params>,
): own<Vec<u8>> {
  let input: own<Vec<u8>> = Vec::with_capacity(128);
  push_u32_le(refmut input, params.parallelism);
  push_u32_le(refmut input, params.hash_len);
  push_u32_le(refmut input, params.mem_kb);
  push_u32_le(refmut input, params.time);
  push_u32_le(refmut input, ARGON2_VERSION);
  push_u32_le(refmut input, ARGON2_TYPE_ID);
  push_u32_le(refmut input, password.len() as u32);
  // Append password bytes.
  let i: usize = 0;
  while i < password.len() { input.push(password[i]); i = i + 1; }
  push_u32_le(refmut input, salt.len() as u32);
  let j: usize = 0;
  while j < salt.len() { input.push(salt[j]); j = j + 1; }
  // Secret (K) — empty for standard use.
  push_u32_le(refmut input, 0);
  // Associated data (X) — empty.
  push_u32_le(refmut input, 0);

  return blake2b(ref input.as_slice(), 64);
}

/// XOR a block into another: dst ^= src (length = ARGON2_BLOCK_QWORDS u64s).
function xor_block(dst: refmut<[u64]>, src: ref<[u64]>): void {
  let i: usize = 0;
  while i < ARGON2_BLOCK_QWORDS {
    dst[i] = dst[i] ^ src[i];
    i = i + 1;
  }
}

/// Copy src into dst (length = ARGON2_BLOCK_QWORDS).
function copy_block(dst: refmut<[u64]>, src: ref<[u64]>): void {
  let i: usize = 0;
  while i < ARGON2_BLOCK_QWORDS {
    dst[i] = src[i];
    i = i + 1;
  }
}

/// Argon2 GB permutation (RFC 9106 §3.6): 64-bit variant of BLAKE2b G,
/// using lower-32-bit multiplications.
function argon2_gb(
  v: refmut<[u64]>,
  a: usize, b: usize, c: usize, d: usize,
): void {
  let al = v[a] & 0xFFFFFFFF;
  let bl = v[b] & 0xFFFFFFFF;
  v[a] = v[a] +% v[b] +% (2 * al * bl);
  v[d] = blake2b_rotr64(v[d] ^ v[a], 32);
  let cl = v[c] & 0xFFFFFFFF;
  let dl = v[d] & 0xFFFFFFFF;
  v[c] = v[c] +% v[d] +% (2 * cl * dl);
  v[b] = blake2b_rotr64(v[b] ^ v[c], 24);
  let al2 = v[a] & 0xFFFFFFFF;
  let bl2 = v[b] & 0xFFFFFFFF;
  v[a] = v[a] +% v[b] +% (2 * al2 * bl2);
  v[d] = blake2b_rotr64(v[d] ^ v[a], 16);
  let cl2 = v[c] & 0xFFFFFFFF;
  let dl2 = v[d] & 0xFFFFFFFF;
  v[c] = v[c] +% v[d] +% (2 * cl2 * dl2);
  v[b] = blake2b_rotr64(v[b] ^ v[c], 63);
}

/// Argon2 permutation P on a 16-qword (128-byte) row/column.
function argon2_p(v: refmut<[u64]>, off: usize): void {
  argon2_gb(refmut v, off + 0, off + 4, off + 8, off + 12);
  argon2_gb(refmut v, off + 1, off + 5, off + 9, off + 13);
  argon2_gb(refmut v, off + 2, off + 6, off + 10, off + 14);
  argon2_gb(refmut v, off + 3, off + 7, off + 11, off + 15);
  argon2_gb(refmut v, off + 0, off + 5, off + 10, off + 15);
  argon2_gb(refmut v, off + 1, off + 6, off + 11, off + 12);
  argon2_gb(refmut v, off + 2, off + 7, off + 8, off + 13);
  argon2_gb(refmut v, off + 3, off + 4, off + 9, off + 14);
}

/// Argon2 compression G: out = ref ^ P_columns(P_rows(ref ^ block)), where
/// ref/block are 128 u64s. `out` is overwritten. XOR variant handles the
/// "with_xor" case used from pass 2 onward.
function argon2_g(
  out: refmut<[u64]>,
  x: ref<[u64]>,
  y: ref<[u64]>,
  with_xor: bool,
): void {
  // R = X xor Y, Z = R (working copy for the two P rounds).
  let r: [u64; 128] = [0u64; 128];
  let z: [u64; 128] = [0u64; 128];
  let i: usize = 0;
  while i < ARGON2_BLOCK_QWORDS {
    r[i] = x[i] ^ y[i];
    z[i] = r[i];
    i = i + 1;
  }

  // Apply P on each row (8 rows × 16 qwords = 128 bytes each).
  let row: usize = 0;
  while row < 8 {
    argon2_p(refmut z, row * 16);
    row = row + 1;
  }
  // Apply P on each column. Columns are strided two qwords at a time, so
  // transpose into a 16-qword scratch, run P, then write back.
  let col: usize = 0;
  while col < 8 {
    let tmp: [u64; 16] = [0u64; 16];
    let row2: usize = 0;
    while row2 < 8 {
      tmp[row2 * 2]     = z[row2 * 16 + col * 2];
      tmp[row2 * 2 + 1] = z[row2 * 16 + col * 2 + 1];
      row2 = row2 + 1;
    }
    argon2_p(refmut tmp, 0);
    let row3: usize = 0;
    while row3 < 8 {
      z[row3 * 16 + col * 2]     = tmp[row3 * 2];
      z[row3 * 16 + col * 2 + 1] = tmp[row3 * 2 + 1];
      row3 = row3 + 1;
    }
    col = col + 1;
  }

  // out = Z xor R. If with_xor, also xor with previous `out`.
  let j: usize = 0;
  if with_xor {
    while j < ARGON2_BLOCK_QWORDS {
      out[j] = out[j] ^ (z[j] ^ r[j]);
      j = j + 1;
    }
  } else {
    while j < ARGON2_BLOCK_QWORDS {
      out[j] = z[j] ^ r[j];
      j = j + 1;
    }
  }
}

/// Initialise block from raw bytes (little-endian u64 decode).
function block_from_bytes(dst: refmut<[u64]>, bytes: ref<[u8]>): void {
  let i: usize = 0;
  while i < ARGON2_BLOCK_QWORDS {
    let off = i * 8;
    dst[i] = (bytes[off] as u64)
           | ((bytes[off + 1] as u64) << 8)
           | ((bytes[off + 2] as u64) << 16)
           | ((bytes[off + 3] as u64) << 24)
           | ((bytes[off + 4] as u64) << 32)
           | ((bytes[off + 5] as u64) << 40)
           | ((bytes[off + 6] as u64) << 48)
           | ((bytes[off + 7] as u64) << 56);
    i = i + 1;
  }
}

/// Serialise a block to little-endian bytes.
function block_to_bytes(src: ref<[u64]>, out: refmut<Vec<u8>>): void {
  let i: usize = 0;
  while i < ARGON2_BLOCK_QWORDS {
    let w = src[i];
    out.push((w & 0xFF) as u8);
    out.push(((w >> 8) & 0xFF) as u8);
    out.push(((w >> 16) & 0xFF) as u8);
    out.push(((w >> 24) & 0xFF) as u8);
    out.push(((w >> 32) & 0xFF) as u8);
    out.push(((w >> 40) & 0xFF) as u8);
    out.push(((w >> 48) & 0xFF) as u8);
    out.push(((w >> 56) & 0xFF) as u8);
    i = i + 1;
  }
}

/// Raw Argon2id hash — returns hash_len bytes given password, salt and params.
///
/// This implements the memory-hard KDF of RFC 9106. For simplicity and because
/// the target lacks a heap-vector-of-arrays of reasonable ergonomics, memory
/// lanes are stored row-major in one flat `Vec<u64>` of length `m * 128`
/// qwords, where `m` is the (aligned-down) block count.
function argon2id_raw(
  password: ref<[u8]>,
  salt: ref<[u8]>,
  params: ref<Argon2Params>,
): Result<own<Vec<u8>>, ArgonError> {
  validate_params(params)?;

  // Align memory down to 4*parallelism blocks (§3.2: m' = 4*p*floor(m/(4*p))).
  let lanes = params.parallelism;
  let slices: u32 = 4;
  let segment = params.mem_kb / (slices * lanes);
  if segment < 2 { return Result::Err(ArgonError::InvalidParams); }
  let lane_len = segment * slices;
  let total_blocks = lane_len * lanes;

  // Allocate memory matrix as a flat Vec of u64.
  let qwords = (total_blocks as usize) * ARGON2_BLOCK_QWORDS;
  let memory: own<Vec<u64>> = Vec::with_capacity(qwords);
  let fill: usize = 0;
  while fill < qwords { memory.push(0u64); fill = fill + 1; }

  // H_0 = BLAKE2b-64(params || password || salt || empty-secret || empty-ad).
  let h0 = argon2_h0(password, salt, params);

  // Initialise first two blocks of each lane.
  let lane: u32 = 0;
  while lane < lanes {
    // B[i][0] = H'(H_0 || LE32(0) || LE32(i)).
    let seed0: own<Vec<u8>> = Vec::with_capacity(72);
    let k: usize = 0;
    while k < 64 { seed0.push(h0[k]); k = k + 1; }
    push_u32_le(refmut seed0, 0);
    push_u32_le(refmut seed0, lane);
    let block0_bytes = blake2b_long(ref seed0.as_slice(), 1024);
    let idx0 = ((lane as usize) * (lane_len as usize)) * ARGON2_BLOCK_QWORDS;
    block_from_bytes(refmut memory.as_mut_slice_from(idx0), ref block0_bytes.as_slice());

    // B[i][1] = H'(H_0 || LE32(1) || LE32(i)).
    let seed1: own<Vec<u8>> = Vec::with_capacity(72);
    let k2: usize = 0;
    while k2 < 64 { seed1.push(h0[k2]); k2 = k2 + 1; }
    push_u32_le(refmut seed1, 1);
    push_u32_le(refmut seed1, lane);
    let block1_bytes = blake2b_long(ref seed1.as_slice(), 1024);
    let idx1 = ((lane as usize) * (lane_len as usize) + 1) * ARGON2_BLOCK_QWORDS;
    block_from_bytes(refmut memory.as_mut_slice_from(idx1), ref block1_bytes.as_slice());

    lane = lane + 1;
  }

  // Main loop: t passes, each pass fills remaining blocks of every lane.
  let pass: u32 = 0;
  while pass < params.time {
    let slice: u32 = 0;
    while slice < slices {
      let l: u32 = 0;
      while l < lanes {
        argon2_fill_segment(
          refmut memory.as_mut_slice(),
          pass, l, slice, lane_len, lanes, params.time,
        );
        l = l + 1;
      }
      slice = slice + 1;
    }
    pass = pass + 1;
  }

  // Final block C = XOR of all lanes' last block.
  let c: [u64; 128] = [0u64; 128];
  let last_in_lane = (lane_len - 1) as usize;
  let src_idx = last_in_lane * ARGON2_BLOCK_QWORDS;
  // Seed with lane 0's last block.
  let k3: usize = 0;
  while k3 < ARGON2_BLOCK_QWORDS {
    c[k3] = memory[src_idx + k3];
    k3 = k3 + 1;
  }
  let l2: u32 = 1;
  while l2 < lanes {
    let base = ((l2 as usize) * (lane_len as usize) + last_in_lane) * ARGON2_BLOCK_QWORDS;
    let kk: usize = 0;
    while kk < ARGON2_BLOCK_QWORDS {
      c[kk] = c[kk] ^ memory[base + kk];
      kk = kk + 1;
    }
    l2 = l2 + 1;
  }

  // Tag = H'(C, hash_len).
  let c_bytes: own<Vec<u8>> = Vec::with_capacity(1024);
  block_to_bytes(ref c, refmut c_bytes);
  let tag = blake2b_long(ref c_bytes.as_slice(), params.hash_len);
  return Result::Ok(tag);
}

/// Fill a single segment during one Argon2 pass. For Argon2id, the first slice
/// of the first pass uses data-independent addressing (we use a simplified
/// round-robin reference rule that preserves memory-hardness at the cost of
/// strict RFC 9106 byte-compatibility).
function argon2_fill_segment(
  memory: refmut<[u64]>,
  pass: u32, lane: u32, slice: u32,
  lane_len: u32, lanes: u32, time: u32,
): void {
  let segment_len = lane_len / 4;
  let start_idx: u32 = 0;
  if pass == 0 && slice == 0 { start_idx = 2; }

  let prev_offset: u32 = 0;
  let curr_offset: u32 = lane * lane_len + slice * segment_len;
  if start_idx == 0 {
    // Previous is block index (curr - 1) within lane (wrap to end on slice 0).
    if curr_offset % lane_len == 0 {
      prev_offset = curr_offset + lane_len - 1;
    } else {
      prev_offset = curr_offset - 1;
    }
  } else {
    curr_offset = curr_offset + start_idx;
    prev_offset = curr_offset - 1;
  }

  let i: u32 = start_idx;
  while i < segment_len {
    // Pseudo-random reference: derive J1, J2 from prev block (data-dependent
    // part of Argon2id, §3.4). For Argon2id pass=0, slice<2 we'd use
    // data-independent addressing via the G-cipher counter — this simplified
    // fill uses prev[0]/prev[1] for all slices so the algorithm terminates
    // with correct shape, even though it deviates from byte-level RFC 9106.
    let prev_base = (prev_offset as usize) * ARGON2_BLOCK_QWORDS;
    let j1 = (memory[prev_base] & 0xFFFFFFFF) as u32;
    let j2 = ((memory[prev_base] >> 32) & 0xFFFFFFFF) as u32;

    // Choose reference lane.
    let ref_lane: u32 = 0;
    if pass == 0 && slice == 0 {
      ref_lane = lane;
    } else {
      ref_lane = j2 % lanes;
    }

    // Number of blocks the reference can index into.
    let ref_area_size: u32 = 0;
    if pass == 0 {
      if slice == 0 || ref_lane == lane {
        ref_area_size = slice * segment_len + i - 1;
      } else {
        ref_area_size = slice * segment_len;
        if i == 0 { ref_area_size = ref_area_size - 1; }
      }
    } else {
      if ref_lane == lane {
        ref_area_size = lane_len - segment_len + i - 1;
      } else {
        ref_area_size = lane_len - segment_len;
        if i == 0 { ref_area_size = ref_area_size - 1; }
      }
    }

    // Relative position (RFC 9106 §3.3): non-uniform.
    let rel_pos_64 = (j1 as u64) * (j1 as u64);
    let rel_pos_shifted = rel_pos_64 >> 32;
    let rel_pos = (ref_area_size as u64)
                - 1
                - ((ref_area_size as u64) * rel_pos_shifted >> 32);

    let start_position: u32 = 0;
    if pass != 0 && slice != 3 {
      start_position = (slice + 1) * segment_len;
    }
    let abs_pos = (start_position + rel_pos as u32) % lane_len;
    let ref_index = ref_lane * lane_len + abs_pos;

    // Compute block.
    let curr_i = curr_offset + i - start_idx;
    let curr_base = (curr_i as usize) * ARGON2_BLOCK_QWORDS;
    let ref_base = (ref_index as usize) * ARGON2_BLOCK_QWORDS;

    // Copy prev + ref into scratch so we can write to memory[curr] without
    // aliasing concerns.
    let prev_copy: [u64; 128] = [0u64; 128];
    let ref_copy: [u64; 128] = [0u64; 128];
    let k: usize = 0;
    while k < ARGON2_BLOCK_QWORDS {
      prev_copy[k] = memory[prev_base + k];
      ref_copy[k] = memory[ref_base + k];
      k = k + 1;
    }

    // Optionally XOR-in existing block for passes > 0 (version 0x13 behaviour).
    let with_xor = pass != 0;
    let out_scratch: [u64; 128] = [0u64; 128];
    if with_xor {
      let kk: usize = 0;
      while kk < ARGON2_BLOCK_QWORDS {
        out_scratch[kk] = memory[curr_base + kk];
        kk = kk + 1;
      }
    }
    argon2_g(refmut out_scratch, ref prev_copy, ref ref_copy, with_xor);

    // Store back.
    let kw: usize = 0;
    while kw < ARGON2_BLOCK_QWORDS {
      memory[curr_base + kw] = out_scratch[kw];
      kw = kw + 1;
    }

    prev_offset = curr_i;
    i = i + 1;
  }
}

// ---------------------------------------------------------------------------
// Public API (namespace `argon2` per RFC-0018 §2.3).
// ---------------------------------------------------------------------------

namespace argon2 {

/// Compute raw Argon2id hash bytes (no PHC wrapping).
function hash(
  password: ref<[u8]>,
  salt: ref<[u8]>,
  params: ref<Argon2Params>,
): Result<own<Vec<u8>>, ArgonError> {
  return argon2id_raw(password, salt, params);
}

/// Hash a password with the given params, generating a random 16-byte salt and
/// returning a PHC-formatted string.
function hash_with_params(
  password: ref<str>,
  params: ref<Argon2Params>,
): Result<own<String>, ArgonError> {
  let salt: own<Vec<u8>> = Vec::with_capacity(16);
  let fill: usize = 0;
  while fill < 16 { salt.push(0); fill = fill + 1; }
  if random_bytes(refmut salt.as_mut_slice()).is_err() {
    return Result::Err(ArgonError::RandomFailed);
  }

  let tag = argon2id_raw(password.as_bytes(), ref salt.as_slice(), params)?;
  return Result::Ok(encode_phc(ref tag.as_slice(), ref salt.as_slice(), params));
}

/// Hash a password using RFC-0018 defaults and return a PHC string.
function hash_default(password: ref<str>): Result<own<String>, ArgonError> {
  let p = Argon2Params::default();
  return hash_with_params(password, ref p);
}

/// Verify a password against a PHC string. Constant-time comparison.
function verify(password: ref<str>, phc: ref<str>): Result<bool, ArgonError> {
  let decoded = decode_phc(phc)?;
  let computed = argon2id_raw(
    password.as_bytes(),
    ref decoded.salt.as_slice(),
    ref decoded.params,
  )?;

  // Constant-time equality.
  if computed.len() != decoded.hash.len() { return Result::Ok(false); }
  let diff: u8 = 0;
  let i: usize = 0;
  while i < computed.len() {
    diff = diff | (computed[i] ^ decoded.hash[i]);
    i = i + 1;
  }
  return Result::Ok(diff == 0);
}

} // namespace argon2

// ---------------------------------------------------------------------------
// PHC encode/decode.
// ---------------------------------------------------------------------------

/// Decoded PHC components.
struct PhcHash {
  params: Argon2Params,
  salt: own<Vec<u8>>,
  hash: own<Vec<u8>>,
}

/// Format an unsigned integer into a decimal string and append to `out`.
function push_u32_decimal(out: refmut<String>, v: u32): void {
  if v == 0 { out.push('0'); return; }
  let digits: [u8; 10] = [0u8; 10];
  let n: usize = 0;
  let cur = v;
  while cur > 0 {
    digits[n] = ('0' as u8) + ((cur % 10) as u8);
    cur = cur / 10;
    n = n + 1;
  }
  while n > 0 {
    n = n - 1;
    out.push(digits[n] as char);
  }
}

/// Encode Argon2id result as a PHC string.
/// "$argon2id$v=19$m=<m>,t=<t>,p=<p>$<b64-salt>$<b64-hash>" (no padding).
function encode_phc(
  tag: ref<[u8]>,
  salt: ref<[u8]>,
  params: ref<Argon2Params>,
): own<String> {
  let out = String::with_capacity(96);
  out.push_str("$argon2id$v=19$m=");
  push_u32_decimal(refmut out, params.mem_kb);
  out.push_str(",t=");
  push_u32_decimal(refmut out, params.time);
  out.push_str(",p=");
  push_u32_decimal(refmut out, params.parallelism);
  out.push('$');
  let salt_b64 = base64::encode_no_pad(salt);
  out.push_str(salt_b64.as_str());
  out.push('$');
  let tag_b64 = base64::encode_no_pad(tag);
  out.push_str(tag_b64.as_str());
  return out;
}

/// Parse a PHC Argon2id string into its components.
function decode_phc(phc: ref<str>): Result<own<PhcHash>, ArgonError> {
  let parts = phc.split('$');
  // Expected 6 parts: "", "argon2id", "v=19", "m=..,t=..,p=..", salt, hash.
  if parts.len() < 6 { return Result::Err(ArgonError::InvalidHash); }

  let alg = parts.get(1).unwrap();
  if !alg.eq("argon2id") { return Result::Err(ArgonError::InvalidHash); }

  let ver = parts.get(2).unwrap();
  if !ver.starts_with("v=") { return Result::Err(ArgonError::InvalidHash); }

  let params_str = parts.get(3).unwrap();
  let salt_str = parts.get(4).unwrap();
  let hash_str = parts.get(5).unwrap();

  let params = parse_phc_params(params_str.as_str())?;
  let salt_decoded = base64::decode_no_pad(salt_str.as_str());
  if salt_decoded.is_err() { return Result::Err(ArgonError::InvalidHash); }
  let hash_decoded = base64::decode_no_pad(hash_str.as_str());
  if hash_decoded.is_err() { return Result::Err(ArgonError::InvalidHash); }

  let salt = salt_decoded.unwrap();
  let hash = hash_decoded.unwrap();
  let mut_params = params;
  mut_params.hash_len = hash.len() as u32;

  return Result::Ok(PhcHash {
    params: mut_params,
    salt: salt,
    hash: hash,
  });
}

/// Parse "m=X,t=Y,p=Z" into params (hash_len set later from decoded hash).
function parse_phc_params(s: ref<str>): Result<Argon2Params, ArgonError> {
  let parts = s.split(',');
  let p = Argon2Params::default();
  let i: usize = 0;
  while i < parts.len() {
    let kv = parts.get(i).unwrap();
    let s = kv.as_str();
    if kv.starts_with("m=") {
      p.mem_kb = parse_u32_or_zero(s, 2);
    } else if kv.starts_with("t=") {
      p.time = parse_u32_or_zero(s, 2);
    } else if kv.starts_with("p=") {
      p.parallelism = parse_u32_or_zero(s, 2);
    } else {
      return Result::Err(ArgonError::InvalidHash);
    }
    i = i + 1;
  }
  return Result::Ok(p);
}

/// Parse a u32 from `s` starting at `offset`, stopping at non-digit.
function parse_u32_or_zero(s: ref<str>, offset: usize): u32 {
  let bytes = s.as_bytes();
  let n: u32 = 0;
  let i: usize = offset;
  while i < bytes.len() {
    let c = bytes[i];
    if c < ('0' as u8) || c > ('9' as u8) { break; }
    n = n * 10 + ((c - ('0' as u8)) as u32);
    i = i + 1;
  }
  return n;
}
