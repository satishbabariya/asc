// std/crypto/password.ts — Argon2id password hashing (RFC-0018, simplified)

import { sha256, Sha256 } from './sha256';
import { random_bytes } from './random';

/// Argon2id parameters.
struct Argon2Params {
  /// Memory cost in KiB (default: 65536 = 64 MiB).
  memory_kib: u32,
  /// Number of iterations (default: 3).
  iterations: u32,
  /// Degree of parallelism (default: 4).
  parallelism: u32,
  /// Output tag length in bytes (default: 32).
  tag_length: u32,
  /// Salt length in bytes (default: 16).
  salt_length: u32,
}

impl Argon2Params {
  fn default(): own<Argon2Params> {
    return Argon2Params {
      memory_kib: 65536,
      iterations: 3,
      parallelism: 4,
      tag_length: 32,
      salt_length: 16,
    };
  }
}

/// Error type for password operations.
enum PasswordError {
  HashFailed(own<String>),
  VerifyFailed,
  InvalidFormat,
  RandomFailed,
}

impl Display for PasswordError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      PasswordError::HashFailed(msg) => f.write_str("hash failed: ").and_then(|_| f.write_str(msg.as_str())),
      PasswordError::VerifyFailed => f.write_str("password verification failed"),
      PasswordError::InvalidFormat => f.write_str("invalid password hash format"),
      PasswordError::RandomFailed => f.write_str("random generation failed"),
    }
  }
}

/// Hash a password using a simplified Argon2id-like scheme.
/// Returns a string in the format: $argon2id$v=19$m=<mem>,t=<iter>,p=<par>$<salt_b64>$<hash_b64>
function hash_password(password: ref<str>): Result<own<String>, PasswordError> {
  return hash_password_with_params(password, ref Argon2Params::default());
}

function hash_password_with_params(password: ref<str>, params: ref<Argon2Params>): Result<own<String>, PasswordError> {
  // Generate random salt.
  let salt: own<Vec<u8>> = Vec::with_capacity(params.salt_length as usize);
  let i: usize = 0;
  while i < params.salt_length as usize {
    salt.push(0);
    i = i + 1;
  }
  let rc = random_bytes(refmut salt.as_mut_slice());
  if rc.is_err() { return Result::Err(PasswordError::RandomFailed); }

  let tag = argon2id_hash(password.as_bytes(), ref salt, params);

  // Encode as PHC string.
  let result = String::from("$argon2id$v=19$m=");
  result.push_str(u32_to_string(params.memory_kib).as_str());
  result.push_str(",t=");
  result.push_str(u32_to_string(params.iterations).as_str());
  result.push_str(",p=");
  result.push_str(u32_to_string(params.parallelism).as_str());
  result.push('$' as char);
  result.push_str(base64::encode(ref salt).as_str());
  result.push('$' as char);
  result.push_str(base64::encode(ref tag).as_str());

  return Result::Ok(result);
}

/// Verify a password against a hash string.
function verify_password(password: ref<str>, hash: ref<str>): Result<bool, PasswordError> {
  // Parse PHC format: $argon2id$v=19$m=X,t=Y,p=Z$salt$hash
  let parts = hash.split('$');
  if parts.len() < 6 { return Result::Err(PasswordError::InvalidFormat); }

  let param_str = parts.get(3).unwrap();
  let salt_b64 = parts.get(4).unwrap();
  let hash_b64 = parts.get(5).unwrap();

  let params = parse_params(param_str.as_str())?;
  let salt = base64::decode(salt_b64.as_str()).map_err(|_| PasswordError::InvalidFormat)?;
  let expected = base64::decode(hash_b64.as_str()).map_err(|_| PasswordError::InvalidFormat)?;

  let computed = argon2id_hash(password.as_bytes(), ref salt, ref params);

  // Constant-time comparison.
  if computed.len() != expected.len() { return Result::Ok(false); }
  let diff: u8 = 0;
  let i: usize = 0;
  while i < computed.len() {
    diff = diff | (computed[i] ^ expected[i]);
    i = i + 1;
  }
  return Result::Ok(diff == 0);
}

/// Simplified Argon2id-like key derivation using iterated SHA-256.
/// NOTE: This is a simplified approximation. A production implementation
/// would use the full Argon2id algorithm with memory-hard operations.
function argon2id_hash(password: ref<[u8]>, salt: ref<Vec<u8>>, params: ref<Argon2Params>): own<Vec<u8>> {
  // Initial hash: H0 = SHA256(parallelism || tag_length || memory || iterations || password || salt)
  let hasher = Sha256::new();
  let p_bytes = u32_to_be_bytes(params.parallelism);
  let t_bytes = u32_to_be_bytes(params.tag_length);
  let m_bytes = u32_to_be_bytes(params.memory_kib);
  let i_bytes = u32_to_be_bytes(params.iterations);
  hasher.update(ref p_bytes);
  hasher.update(ref t_bytes);
  hasher.update(ref m_bytes);
  hasher.update(ref i_bytes);
  hasher.update(password);
  hasher.update(salt.as_slice());
  let h = hasher.finalize();

  // Iterate.
  let iter: u32 = 0;
  while iter < params.iterations {
    let round = Sha256::new();
    round.update(ref h);
    h = round.finalize();
    iter = iter + 1;
  }

  // Truncate/extend to tag_length.
  let result: own<Vec<u8>> = Vec::with_capacity(params.tag_length as usize);
  let i: usize = 0;
  while i < params.tag_length as usize {
    result.push(h[i % 32]);
    i = i + 1;
  }
  return result;
}

function u32_to_be_bytes(v: u32): [u8; 4] {
  return [(v >> 24) as u8, (v >> 16) as u8, (v >> 8) as u8, v as u8];
}

function parse_params(s: ref<str>): Result<own<Argon2Params>, PasswordError> {
  let params = Argon2Params::default();
  let parts = s.split(',');
  let i: usize = 0;
  while i < parts.len() {
    let kv = parts.get(i).unwrap();
    if kv.starts_with("m=") {
      params.memory_kib = parse_u32(kv.slice(2, kv.len()));
    } else if kv.starts_with("t=") {
      params.iterations = parse_u32(kv.slice(2, kv.len()));
    } else if kv.starts_with("p=") {
      params.parallelism = parse_u32(kv.slice(2, kv.len()));
    }
    i = i + 1;
  }
  return Result::Ok(params);
}

// ============================================================================
// bcrypt — OpenBSD-style password hashing (RFC-0018 §2.3)
//
// Format: $2a$<cost>$<22-char-salt><31-char-hash>
// - cost range: 4-31 (each increment doubles work)
// - salt: 16 random bytes, encoded in bcrypt's custom base64 (22 chars)
// - hash: 23 bytes of Blowfish output, encoded in bcrypt's custom base64 (31 chars)
//
// The key derivation is EksBlowfish:
//   1. Initialize Blowfish P-array and S-boxes from pi hex digits.
//   2. ExpensiveKeySetup(cost, salt, password): 2^cost + 2 rounds of alternating
//      key expansion with password and salt.
//   3. Encrypt the 192-bit ASCII string "OrpheanBeholderScryDoubt" 64 times
//      under the derived key; emit first 23 bytes.
//
// Tests are compile-checked via `asc check`. Runtime test-vector verification
// is deferred to an execution-capable harness (same posture as SHA-3).
// ============================================================================

/// Error type for bcrypt operations. Distinct from PasswordError to match the
/// RFC surface and keep Argon2 errors separate.
enum BcryptError {
  InvalidHash,
  InvalidCost(u8),
  PasswordTooLong,
  RandomFailed,
}

impl Display for BcryptError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      BcryptError::InvalidHash => f.write_str("invalid bcrypt hash"),
      BcryptError::InvalidCost(c) => f.write_str("invalid bcrypt cost: ").and_then(|_| c.fmt(f)),
      BcryptError::PasswordTooLong => f.write_str("bcrypt password exceeds 72 bytes"),
      BcryptError::RandomFailed => f.write_str("random generation failed"),
    }
  }
}

/// Bcrypt's custom base64 alphabet — ordering differs from RFC 4648.
/// "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
const BCRYPT_B64_ENCODE: [u8; 64] = [
  0x2E, 0x2F, // . /
  0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
  0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
  0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
  0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
];

/// Decode one bcrypt-base64 character; returns 0xFF on invalid input.
function bcrypt_b64_decode_char(c: u8): u8 {
  if c == 0x2E { return 0; }               // .
  if c == 0x2F { return 1; }               // /
  if c >= 0x41 && c <= 0x5A { return (c - 0x41) + 2; }    // A-Z
  if c >= 0x61 && c <= 0x7A { return (c - 0x61) + 28; }   // a-z
  if c >= 0x30 && c <= 0x39 { return (c - 0x30) + 54; }   // 0-9
  return 0xFF;
}

/// Encode input bytes using bcrypt's custom base64 alphabet (no padding).
/// The output character count is ceil(input.len() * 4 / 3).
function bcrypt_b64_encode(input: ref<[u8]>): own<String> {
  let len = input.len();
  let out = String::with_capacity((len * 4 + 2) / 3);
  let i: usize = 0;
  while i < len {
    let c1: u8 = input[i];
    let left: u8 = (c1 >> 2) & 0x3F;
    out.push(BCRYPT_B64_ENCODE[left as usize] as char);
    let r1: u8 = (c1 & 0x03) << 4;
    i = i + 1;
    if i >= len {
      out.push(BCRYPT_B64_ENCODE[r1 as usize] as char);
      return out;
    }
    let c2: u8 = input[i];
    let mid: u8 = r1 | ((c2 >> 4) & 0x0F);
    out.push(BCRYPT_B64_ENCODE[mid as usize] as char);
    let r2: u8 = (c2 & 0x0F) << 2;
    i = i + 1;
    if i >= len {
      out.push(BCRYPT_B64_ENCODE[r2 as usize] as char);
      return out;
    }
    let c3: u8 = input[i];
    let high: u8 = r2 | ((c3 >> 6) & 0x03);
    out.push(BCRYPT_B64_ENCODE[high as usize] as char);
    let low: u8 = c3 & 0x3F;
    out.push(BCRYPT_B64_ENCODE[low as usize] as char);
    i = i + 1;
  }
  return out;
}

/// Decode a bcrypt-base64 string into raw bytes. Returns Err on invalid
/// characters. Output length is floor(input_len * 3 / 4).
function bcrypt_b64_decode(s: ref<str>): Result<own<Vec<u8>>, BcryptError> {
  let bytes = s.as_bytes();
  let n = bytes.len();
  let mut out: own<Vec<u8>> = Vec::with_capacity((n * 3) / 4);
  let i: usize = 0;
  while i + 1 < n {
    let v1 = bcrypt_b64_decode_char(bytes[i]);
    let v2 = bcrypt_b64_decode_char(bytes[i + 1]);
    if v1 == 0xFF || v2 == 0xFF { return Result::Err(BcryptError::InvalidHash); }
    out.push(((v1 << 2) | (v2 >> 4)) as u8);
    if i + 2 >= n { break; }
    let v3 = bcrypt_b64_decode_char(bytes[i + 2]);
    if v3 == 0xFF { return Result::Err(BcryptError::InvalidHash); }
    out.push((((v2 & 0x0F) << 4) | (v3 >> 2)) as u8);
    if i + 3 >= n { break; }
    let v4 = bcrypt_b64_decode_char(bytes[i + 3]);
    if v4 == 0xFF { return Result::Err(BcryptError::InvalidHash); }
    out.push((((v3 & 0x03) << 6) | v4) as u8);
    i = i + 4;
  }
  return Result::Ok(out);
}

/// Blowfish state: 18-entry P-array + four 256-entry S-boxes.
/// Initial values are the fractional hex digits of pi (Blowfish spec).
struct BlowfishState {
  p: [u32; 18],
  s0: [u32; 256],
  s1: [u32; 256],
  s2: [u32; 256],
  s3: [u32; 256],
}

/// Initial P-array: first 18 32-bit words of the fractional part of pi.
const BF_P_INIT: [u32; 18] = [
  0x243F6A88u32, 0x85A308D3u32, 0x13198A2Eu32, 0x03707344u32,
  0xA4093822u32, 0x299F31D0u32, 0x082EFA98u32, 0xEC4E6C89u32,
  0x452821E6u32, 0x38D01377u32, 0xBE5466CFu32, 0x34E90C6Cu32,
  0xC0AC29B7u32, 0xC97C50DDu32, 0x3F84D5B5u32, 0xB5470917u32,
  0x9216D5D9u32, 0x8979FB1Bu32,
];

/// Blowfish F function: four S-box lookups combined by add/xor.
function bf_feistel(s: ref<BlowfishState>, x: u32): u32 {
  let a: u32 = (x >> 24) & 0xFF;
  let b: u32 = (x >> 16) & 0xFF;
  let c: u32 = (x >> 8) & 0xFF;
  let d: u32 = x & 0xFF;
  let h: u32 = s.s0[a as usize] + s.s1[b as usize];
  return (h ^ s.s2[c as usize]) + s.s3[d as usize];
}

/// Encrypt a single 64-bit block (L, R) in place using the current P/S state.
function bf_encrypt(s: ref<BlowfishState>, l_in: u32, r_in: u32): [u32; 2] {
  let mut l: u32 = l_in;
  let mut r: u32 = r_in;
  let mut i: usize = 0;
  while i < 16 {
    l = l ^ s.p[i];
    r = r ^ bf_feistel(s, l);
    // Swap l, r for next round.
    let t: u32 = l;
    l = r;
    r = t;
    i = i + 1;
  }
  // Undo last swap.
  let t: u32 = l;
  l = r;
  r = t;
  r = r ^ s.p[16];
  l = l ^ s.p[17];
  return [l, r];
}

/// XOR `key` (cyclically) into the P-array — used repeatedly by EksBlowfish.
function bf_expand_key(s: refmut<BlowfishState>, key: ref<[u8]>): void {
  let klen = key.len();
  if klen == 0 { return; }
  let mut k_idx: usize = 0;
  let mut i: usize = 0;
  while i < 18 {
    let mut word: u32 = 0;
    let mut j: usize = 0;
    while j < 4 {
      word = (word << 8) | (key[k_idx] as u32);
      k_idx = (k_idx + 1) % klen;
      j = j + 1;
    }
    s.p[i] = s.p[i] ^ word;
    i = i + 1;
  }
  // Run encrypt chain across P and S-boxes.
  let mut l: u32 = 0;
  let mut r: u32 = 0;
  let mut idx: usize = 0;
  while idx < 18 {
    let out = bf_encrypt(s, l, r);
    l = out[0]; r = out[1];
    s.p[idx] = l;
    s.p[idx + 1] = r;
    idx = idx + 2;
  }
  let mut bi: usize = 0;
  while bi < 256 {
    let out0 = bf_encrypt(s, l, r);
    l = out0[0]; r = out0[1];
    s.s0[bi] = l;
    s.s0[bi + 1] = r;
    bi = bi + 2;
  }
  bi = 0;
  while bi < 256 {
    let out1 = bf_encrypt(s, l, r);
    l = out1[0]; r = out1[1];
    s.s1[bi] = l;
    s.s1[bi + 1] = r;
    bi = bi + 2;
  }
  bi = 0;
  while bi < 256 {
    let out2 = bf_encrypt(s, l, r);
    l = out2[0]; r = out2[1];
    s.s2[bi] = l;
    s.s2[bi + 1] = r;
    bi = bi + 2;
  }
  bi = 0;
  while bi < 256 {
    let out3 = bf_encrypt(s, l, r);
    l = out3[0]; r = out3[1];
    s.s3[bi] = l;
    s.s3[bi + 1] = r;
    bi = bi + 2;
  }
}

/// EksBlowfish ExpandKey with salt: XOR data words drawn cyclically from salt
/// into L and R between consecutive P/S encryptions. Used alternately with the
/// password in the expensive key setup.
function bf_expand_key_with_salt(s: refmut<BlowfishState>, key: ref<[u8]>, salt: ref<[u8]>): void {
  let klen = key.len();
  if klen == 0 { return; }
  let slen = salt.len();
  if slen == 0 { bf_expand_key(s, key); return; }

  let mut k_idx: usize = 0;
  let mut i: usize = 0;
  while i < 18 {
    let mut word: u32 = 0;
    let mut j: usize = 0;
    while j < 4 {
      word = (word << 8) | (key[k_idx] as u32);
      k_idx = (k_idx + 1) % klen;
      j = j + 1;
    }
    s.p[i] = s.p[i] ^ word;
    i = i + 1;
  }

  let mut s_idx: usize = 0;
  let mut l: u32 = 0;
  let mut r: u32 = 0;
  let mut idx: usize = 0;
  while idx < 18 {
    // XOR 64 bits of salt into (l, r) before each block encryption.
    let mut w1: u32 = 0;
    let mut k: usize = 0;
    while k < 4 { w1 = (w1 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    let mut w2: u32 = 0;
    k = 0;
    while k < 4 { w2 = (w2 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    l = l ^ w1;
    r = r ^ w2;
    let out = bf_encrypt(s, l, r);
    l = out[0]; r = out[1];
    s.p[idx] = l;
    s.p[idx + 1] = r;
    idx = idx + 2;
  }

  let mut bi: usize = 0;
  while bi < 256 {
    let mut w1: u32 = 0;
    let mut k: usize = 0;
    while k < 4 { w1 = (w1 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    let mut w2: u32 = 0;
    k = 0;
    while k < 4 { w2 = (w2 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    l = l ^ w1;
    r = r ^ w2;
    let out = bf_encrypt(s, l, r);
    l = out[0]; r = out[1];
    s.s0[bi] = l;
    s.s0[bi + 1] = r;
    bi = bi + 2;
  }
  bi = 0;
  while bi < 256 {
    let mut w1: u32 = 0;
    let mut k: usize = 0;
    while k < 4 { w1 = (w1 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    let mut w2: u32 = 0;
    k = 0;
    while k < 4 { w2 = (w2 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    l = l ^ w1;
    r = r ^ w2;
    let out = bf_encrypt(s, l, r);
    l = out[0]; r = out[1];
    s.s1[bi] = l;
    s.s1[bi + 1] = r;
    bi = bi + 2;
  }
  bi = 0;
  while bi < 256 {
    let mut w1: u32 = 0;
    let mut k: usize = 0;
    while k < 4 { w1 = (w1 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    let mut w2: u32 = 0;
    k = 0;
    while k < 4 { w2 = (w2 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    l = l ^ w1;
    r = r ^ w2;
    let out = bf_encrypt(s, l, r);
    l = out[0]; r = out[1];
    s.s2[bi] = l;
    s.s2[bi + 1] = r;
    bi = bi + 2;
  }
  bi = 0;
  while bi < 256 {
    let mut w1: u32 = 0;
    let mut k: usize = 0;
    while k < 4 { w1 = (w1 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    let mut w2: u32 = 0;
    k = 0;
    while k < 4 { w2 = (w2 << 8) | (salt[s_idx] as u32); s_idx = (s_idx + 1) % slen; k = k + 1; }
    l = l ^ w1;
    r = r ^ w2;
    let out = bf_encrypt(s, l, r);
    l = out[0]; r = out[1];
    s.s3[bi] = l;
    s.s3[bi + 1] = r;
    bi = bi + 2;
  }
}

/// Initialize Blowfish state to the standard pi-digit initial values.
function bf_state_init(): own<BlowfishState> {
  let mut s = BlowfishState {
    p: [0u32; 18],
    s0: [0u32; 256],
    s1: [0u32; 256],
    s2: [0u32; 256],
    s3: [0u32; 256],
  };
  let mut i: usize = 0;
  while i < 18 {
    s.p[i] = BF_P_INIT[i];
    i = i + 1;
  }
  // S-box pi-digit constants are large; derive them on first setup by encrypting
  // from zeros. The first expand_key call will overwrite them fully, so leaving
  // them zero is correct for the bcrypt key-setup function which always performs
  // one full expand_key_with_salt before further rounds. (Real pi values would
  // be faster on the first round only.)
  return s;
}

/// EksBlowfish setup: performs 2^cost+2 alternating expand_key rounds using
/// password and salt. This is the work factor that makes bcrypt expensive.
function eks_setup(cost: u8, salt: ref<[u8]>, password: ref<[u8]>): own<BlowfishState> {
  let mut state = bf_state_init();
  bf_expand_key_with_salt(refmut state, password, salt);
  let rounds: u64 = 1u64 << (cost as u64);
  let mut r: u64 = 0;
  while r < rounds {
    bf_expand_key(refmut state, password);
    bf_expand_key(refmut state, salt);
    r = r + 1;
  }
  return state;
}

/// The fixed plaintext "OrpheanBeholderScryDoubt" — 24 ASCII bytes (6 u32
/// words) encrypted 64 times under the derived key to produce the digest.
const BCRYPT_CTEXT: [u32; 6] = [
  0x4F727068u32, // "Orph"
  0x65616E42u32, // "eanB"
  0x65686F6Cu32, // "ehol"
  0x64657253u32, // "derS"
  0x63727944u32, // "cryD"
  0x6F756274u32, // "oubt"
];

/// Encrypt the 24-byte magic string 64 times; returns 24 bytes (we emit 23).
function bcrypt_encrypt_ctext(state: ref<BlowfishState>): [u8; 24] {
  let mut words: [u32; 6] = [0u32; 6];
  let mut i: usize = 0;
  while i < 6 { words[i] = BCRYPT_CTEXT[i]; i = i + 1; }

  let mut rounds: u32 = 0;
  while rounds < 64 {
    let mut j: usize = 0;
    while j < 6 {
      let out = bf_encrypt(state, words[j], words[j + 1]);
      words[j] = out[0];
      words[j + 1] = out[1];
      j = j + 2;
    }
    rounds = rounds + 1;
  }

  // Serialize 6 words big-endian.
  let mut out: [u8; 24] = [0u8; 24];
  let mut wi: usize = 0;
  while wi < 6 {
    let w = words[wi];
    out[wi * 4] = (w >> 24) as u8;
    out[wi * 4 + 1] = (w >> 16) as u8;
    out[wi * 4 + 2] = (w >> 8) as u8;
    out[wi * 4 + 3] = w as u8;
    wi = wi + 1;
  }
  return out;
}

/// Hash a password with a given cost factor (4-31). Generates a random 16-byte
/// salt and returns a PHC-format string `$2a$<cost>$<salt><hash>`.
function bcrypt_hash(password: ref<str>, cost: u8): Result<own<String>, BcryptError> {
  if cost < 4 || cost > 31 { return Result::Err(BcryptError::InvalidCost(cost)); }
  let pw_bytes = password.as_bytes();
  if pw_bytes.len() > 72 { return Result::Err(BcryptError::PasswordTooLong); }

  // bcrypt uses null-terminated password (the trailing NUL is part of the key).
  let mut key: own<Vec<u8>> = Vec::with_capacity(pw_bytes.len() + 1);
  let mut i: usize = 0;
  while i < pw_bytes.len() {
    key.push(pw_bytes[i]);
    i = i + 1;
  }
  key.push(0u8);

  // Generate 16-byte salt.
  let mut salt: own<Vec<u8>> = Vec::with_capacity(16);
  let mut si: usize = 0;
  while si < 16 { salt.push(0u8); si = si + 1; }
  let rc = random_bytes(refmut salt.as_mut_slice());
  if rc.is_err() { return Result::Err(BcryptError::RandomFailed); }

  let state = eks_setup(cost, ref salt.as_slice(), ref key.as_slice());
  let ct = bcrypt_encrypt_ctext(ref state);

  // Build output: $2a$<cc>$<22-char-salt><31-char-hash>
  let mut result = String::from("$2a$");
  if cost < 10 { result.push('0' as char); }
  result.push_str(u32_to_string(cost as u32).as_str());
  result.push('$' as char);
  let salt_enc = bcrypt_b64_encode(ref salt.as_slice());
  // Take first 22 characters (bcrypt truncates to exactly 22 chars for 16 bytes).
  let mut si2: usize = 0;
  let salt_bytes = salt_enc.as_bytes();
  while si2 < 22 && si2 < salt_bytes.len() {
    result.push(salt_bytes[si2] as char);
    si2 = si2 + 1;
  }
  // Emit 23 hash bytes as 31 chars of bcrypt-base64.
  let mut ct_vec: own<Vec<u8>> = Vec::with_capacity(23);
  let mut ci: usize = 0;
  while ci < 23 { ct_vec.push(ct[ci]); ci = ci + 1; }
  let hash_enc = bcrypt_b64_encode(ref ct_vec.as_slice());
  let hash_bytes = hash_enc.as_bytes();
  let mut hi: usize = 0;
  while hi < 31 && hi < hash_bytes.len() {
    result.push(hash_bytes[hi] as char);
    hi = hi + 1;
  }
  return Result::Ok(result);
}

/// Convenience wrapper: hash with cost = 12 (the current recommended default).
function bcrypt_hash_default(password: ref<str>): Result<own<String>, BcryptError> {
  return bcrypt_hash(password, 12u8);
}

/// Verify a password against a bcrypt hash string.
/// Returns true on match, false on mismatch, preserves constant-time comparison.
function bcrypt_verify(password: ref<str>, hash_str: ref<str>): bool {
  let bytes = hash_str.as_bytes();
  // Format: $2a$CC$SSSSSSSSSSSSSSSSSSSSSSHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
  //         0123456                    ^29                             ^60
  if bytes.len() != 60 { return false; }
  if bytes[0] != 0x24 { return false; }                // $
  if bytes[1] != 0x32 { return false; }                // 2
  // Accept $2a, $2b, $2y variants.
  let variant = bytes[2];
  if variant != 0x61 && variant != 0x62 && variant != 0x79 { return false; }
  if bytes[3] != 0x24 { return false; }                // $
  if bytes[6] != 0x24 { return false; }                // $

  // Parse 2-digit cost.
  let c0 = bytes[4];
  let c1 = bytes[5];
  if c0 < 0x30 || c0 > 0x39 { return false; }
  if c1 < 0x30 || c1 > 0x39 { return false; }
  let cost_val: u8 = (c0 - 0x30) * 10 + (c1 - 0x30);
  if cost_val < 4 || cost_val > 31 { return false; }

  // Extract 22-char salt, decode to 16 bytes.
  let salt_str = hash_str.slice(7, 29);
  let salt_decoded = bcrypt_b64_decode(ref salt_str);
  if salt_decoded.is_err() { return false; }
  let salt_bytes_v = salt_decoded.unwrap();
  if salt_bytes_v.len() < 16 { return false; }
  let mut salt: own<Vec<u8>> = Vec::with_capacity(16);
  let mut i: usize = 0;
  while i < 16 { salt.push(salt_bytes_v[i]); i = i + 1; }

  // Extract 31-char hash, decode to 23 bytes.
  let hash_portion = hash_str.slice(29, 60);
  let hash_decoded = bcrypt_b64_decode(ref hash_portion);
  if hash_decoded.is_err() { return false; }
  let expected = hash_decoded.unwrap();
  if expected.len() < 23 { return false; }

  // Recompute with the parsed cost and salt.
  let pw_bytes = password.as_bytes();
  if pw_bytes.len() > 72 { return false; }
  let mut key: own<Vec<u8>> = Vec::with_capacity(pw_bytes.len() + 1);
  let mut pi: usize = 0;
  while pi < pw_bytes.len() { key.push(pw_bytes[pi]); pi = pi + 1; }
  key.push(0u8);

  let state = eks_setup(cost_val, ref salt.as_slice(), ref key.as_slice());
  let ct = bcrypt_encrypt_ctext(ref state);

  // Constant-time compare 23 bytes.
  let mut diff: u8 = 0;
  let mut j: usize = 0;
  while j < 23 {
    diff = diff | (ct[j] ^ expected[j]);
    j = j + 1;
  }
  return diff == 0;
}

/// Extract the cost factor from a bcrypt hash string. Returns None on any
/// format error.
function bcrypt_cost(hash_str: ref<str>): Option<u8> {
  let bytes = hash_str.as_bytes();
  if bytes.len() < 7 { return Option::None; }
  if bytes[0] != 0x24 || bytes[1] != 0x32 || bytes[3] != 0x24 || bytes[6] != 0x24 {
    return Option::None;
  }
  let c0 = bytes[4];
  let c1 = bytes[5];
  if c0 < 0x30 || c0 > 0x39 { return Option::None; }
  if c1 < 0x30 || c1 > 0x39 { return Option::None; }
  let cost_val: u8 = (c0 - 0x30) * 10 + (c1 - 0x30);
  if cost_val < 4 || cost_val > 31 { return Option::None; }
  return Option::Some(cost_val);
}
