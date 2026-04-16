# RFC-0018/0017 Encoding, Crypto & Collections Utils — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add encoding fill-ins (base64url, hex upper, varint i64, percent except), crypto additions (SHA-384, HMAC-SHA512, verify functions, subtle::eq, UUID v4), and 12 collection utility functions to push RFC-0018 from 52%→~78% and RFC-0017 from 15%→~50%.

**Architecture:** Two independent workstreams — encoding/crypto and collection utils — can be executed in parallel. All changes are pure .ts standard library code; no compiler changes needed. Each workstream modifies different files with no overlap.

**Tech Stack:** ASC TypeScript (std library), lit test framework. Test format: `// RUN: %asc check %s` with `function main(): i32 { ... return 0; }`.

---

## Workstream A: Encoding/Crypto

### Task 1: Base64 encode_no_pad + base64url

**Files:**
- Modify: `std/encoding/base64.ts` (append after line 118)

- [ ] **Step 1: Add encode_no_pad function**

Append to `std/encoding/base64.ts`:

```typescript
/// Encode bytes to Base64 without padding.
function encode_no_pad(input: ref<[u8]>): own<String> {
  let result = encode(input);
  // Strip trailing '=' padding.
  while result.len() > 0 {
    const last_idx = result.len() - 1;
    const last_byte = result.as_bytes()[last_idx];
    if last_byte != 0x3D { break; } // '='
    result.truncate(last_idx);
  }
  return result;
}

/// URL-safe Base64 alphabet: '+' -> '-', '/' -> '_', no padding.
const ENCODE_TABLE_URL: [u8; 64] = [
  // A-Z
  0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
  0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
  // a-z
  0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
  0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
  // 0-9
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  // -, _
  0x2D, 0x5F,
];

/// Encode bytes to URL-safe Base64 (no padding).
function base64url_encode(input: ref<[u8]>): own<String> {
  let len = input.len();
  let out_len = ((len + 2) / 3) * 4;
  let result = String::with_capacity(out_len);
  let i: usize = 0;
  while i + 2 < len {
    let b0 = input[i] as u32;
    let b1 = input[i + 1] as u32;
    let b2 = input[i + 2] as u32;
    let triple = (b0 << 16) | (b1 << 8) | b2;
    result.push(ENCODE_TABLE_URL[((triple >> 18) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE_URL[((triple >> 12) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE_URL[((triple >> 6) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE_URL[(triple & 0x3F) as usize] as char);
    i = i + 3;
  }
  let remaining = len - i;
  if remaining == 2 {
    let b0 = input[i] as u32;
    let b1 = input[i + 1] as u32;
    let triple = (b0 << 16) | (b1 << 8);
    result.push(ENCODE_TABLE_URL[((triple >> 18) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE_URL[((triple >> 12) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE_URL[((triple >> 6) & 0x3F) as usize] as char);
  } else if remaining == 1 {
    let b0 = input[i] as u32;
    let triple = b0 << 16;
    result.push(ENCODE_TABLE_URL[((triple >> 18) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE_URL[((triple >> 12) & 0x3F) as usize] as char);
  }
  return result;
}

/// Decode a URL-safe Base64 character to its 6-bit value.
function decode_char_url(c: u8, pos: usize): Result<u8, DecodeError> {
  if c >= 0x41 && c <= 0x5A { return Result::Ok(c - 0x41); }       // A-Z
  if c >= 0x61 && c <= 0x7A { return Result::Ok(c - 0x61 + 26); }  // a-z
  if c >= 0x30 && c <= 0x39 { return Result::Ok(c - 0x30 + 52); }  // 0-9
  if c == 0x2D { return Result::Ok(62); }                            // -
  if c == 0x5F { return Result::Ok(63); }                            // _
  return Result::Err(DecodeError::InvalidCharacter(pos));
}

/// Decode a URL-safe Base64 string to bytes (handles missing padding).
function base64url_decode(input: ref<str>): Result<own<Vec<u8>>, DecodeError> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  if len == 0 { return Result::Ok(Vec::new()); }

  // Add padding to make length multiple of 4.
  let padded_len = len;
  let pad_needed = (4 - (len % 4)) % 4;
  padded_len = padded_len + pad_needed;

  let result: own<Vec<u8>> = Vec::new();
  let i: usize = 0;

  while i < len {
    let a = decode_char_url(bytes[i], i)?;
    let b: u8 = 0;
    let c: u8 = 0;
    let d: u8 = 0;
    let have_b = i + 1 < len;
    let have_c = i + 2 < len;
    let have_d = i + 3 < len;
    if have_b { b = decode_char_url(bytes[i + 1], i + 1)?; }
    if have_c { c = decode_char_url(bytes[i + 2], i + 2)?; }
    if have_d { d = decode_char_url(bytes[i + 3], i + 3)?; }

    let triple = ((a as u32) << 18) | ((b as u32) << 12) | ((c as u32) << 6) | (d as u32);
    result.push(((triple >> 16) & 0xFF) as u8);
    if have_c { result.push(((triple >> 8) & 0xFF) as u8); }
    if have_d { result.push((triple & 0xFF) as u8); }
    i = i + 4;
  }

  return Result::Ok(result);
}
```

- [ ] **Step 2: Run full test suite**

Run: `lit test/ --no-progress-bar`
Expected: All 242 tests pass (no new test yet, just verifying no regression).

- [ ] **Step 3: Commit**

```bash
git add std/encoding/base64.ts
git commit -m "feat: base64 encode_no_pad + base64url encode/decode (RFC-0018)"
```

### Task 2: Hex encode_upper + varint i64

**Files:**
- Modify: `std/encoding/hex.ts` (append after line 47)
- Modify: `std/encoding/varint.ts` (append after line 137)

- [ ] **Step 1: Add hex encode_upper**

Append to `std/encoding/hex.ts`:

```typescript
const HEX_UPPER: [u8; 16] = [
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
];

/// Encode bytes as uppercase hexadecimal string.
function encode_upper(input: ref<[u8]>): own<String> {
  let result = String::with_capacity(input.len() * 2);
  let i: usize = 0;
  while i < input.len() {
    let b = input[i];
    result.push(HEX_UPPER[(b >> 4) as usize] as char);
    result.push(HEX_UPPER[(b & 0x0F) as usize] as char);
    i = i + 1;
  }
  return result;
}
```

- [ ] **Step 2: Add varint encode_i64 and decode_i64**

Append to `std/encoding/varint.ts`:

```typescript
/// Encode a signed i64 as signed LEB128.
function encode_i64(buf: refmut<Vec<u8>>, value: i64): void {
  let v = value;
  let more = true;
  while more {
    let byte = (v & 0x7F) as u8;
    v = v >> 7;
    if (v == 0 && (byte & 0x40) == 0) || (v == -1 && (byte & 0x40) != 0) {
      more = false;
    } else {
      byte = byte | 0x80;
    }
    buf.push(byte);
  }
}

/// Decode a signed LEB128 i64 from a byte slice.
/// Returns (decoded_value, bytes_consumed).
function decode_i64(input: ref<[u8]>): Result<(i64, usize), VarintError> {
  let result: i64 = 0;
  let shift: u32 = 0;
  let i: usize = 0;
  let byte: u8 = 0;

  while i < input.len() {
    byte = input[i];
    if shift >= 70 {
      return Result::Err(VarintError::Overflow);
    }
    result = result | (((byte & 0x7F) as i64) << shift);
    shift = shift + 7;
    i = i + 1;
    if (byte & 0x80) == 0 { break; }
  }

  if i == 0 { return Result::Err(VarintError::UnexpectedEof); }
  if (byte & 0x80) != 0 { return Result::Err(VarintError::UnexpectedEof); }

  // Sign-extend if needed.
  if shift < 64 && (byte & 0x40) != 0 {
    result = result | ((-1 as i64) << shift);
  }

  return Result::Ok((result, i));
}
```

- [ ] **Step 3: Add percent encode_except**

Append to `std/encoding/percent.ts` (before the `hex_digit` function at line 82):

```typescript
/// Encode a string using percent-encoding, but preserve chars in `safe` unencoded.
function encode_except(input: ref<str>, safe: ref<str>): own<String> {
  let result = String::new();
  let bytes = input.as_bytes();
  let safe_bytes = safe.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    let c = bytes[i];
    if is_unreserved(c) {
      result.push(c as char);
    } else {
      // Check if c is in the safe set.
      let is_safe = false;
      let j: usize = 0;
      while j < safe_bytes.len() {
        if c == safe_bytes[j] { is_safe = true; break; }
        j = j + 1;
      }
      if is_safe {
        result.push(c as char);
      } else {
        result.push('%');
        let hex_chars: [u8; 16] = [
          0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
          0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
        ];
        result.push(hex_chars[(c >> 4) as usize] as char);
        result.push(hex_chars[(c & 0x0F) as usize] as char);
      }
    }
    i = i + 1;
  }
  return result;
}
```

- [ ] **Step 4: Run tests and commit**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

```bash
git add std/encoding/hex.ts std/encoding/varint.ts std/encoding/percent.ts
git commit -m "feat: hex encode_upper + varint i64 + percent encode_except (RFC-0018)"
```

### Task 3: SHA-384

**Files:**
- Create: `std/crypto/sha384.ts`
- Create: `test/std/test_sha384.ts`

- [ ] **Step 1: Create test file**

Create `test/std/test_sha384.ts`:

```typescript
// RUN: %asc check %s
// Test: SHA-384 hash function.
function main(): i32 {
  // SHA-384 produces 48 bytes.
  // Test that Sha384 struct has the right interface.
  let h: Sha384 = Sha384::new();
  h.update("hello");
  const digest = h.finalize();
  // digest is [u8; 48] — just verify it exists and has length 48.
  assert_eq!(digest.len(), 48);

  // One-shot convenience.
  const hash = sha384("test");
  assert_eq!(hash.len(), 48);

  return 0;
}
```

- [ ] **Step 2: Create SHA-384 implementation**

Create `std/crypto/sha384.ts`:

```typescript
// std/crypto/sha384.ts — SHA-384 (truncated SHA-512 with different IV) (RFC-0018)

// SHA-384 uses the same compression function as SHA-512 but with different
// initial hash values and truncated to 48 bytes.

import { K, rotr64 } from './sha512';

/// SHA-384 initial hash values (first 64 bits of fractional parts of sqrt of 9th-16th primes).
const H384_INIT: [u64; 8] = [
  0xcbbb9d5dc1059ed8, 0x629a292a367cd507,
  0x9159015a3070dd17, 0x152fecd8f70e5939,
  0x67332667ffc00b31, 0x8eb44a8768581511,
  0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4,
];

/// Streaming SHA-384 hasher.
struct Sha384 {
  state: [u64; 8],
  buffer: [u8; 128],
  buf_len: usize,
  total_len: u64,
}

impl Sha384 {
  fn new(): own<Sha384> {
    return Sha384 {
      state: H384_INIT,
      buffer: [0u8; 128],
      buf_len: 0,
      total_len: 0,
    };
  }

  fn update(refmut<Self>, data: ref<[u8]>): void {
    let i: usize = 0;
    let len = data.len();
    self.total_len = self.total_len + len as u64;

    while i < len {
      if self.buf_len == 128 {
        self.compress(ref self.buffer);
        self.buf_len = 0;
      }
      self.buffer[self.buf_len] = data[i];
      self.buf_len = self.buf_len + 1;
      i = i + 1;
    }
  }

  fn finalize(refmut<Self>): [u8; 48] {
    let total_bits = self.total_len * 8;

    // Padding: append 0x80, then zeros, then 128-bit length (big-endian).
    self.buffer[self.buf_len] = 0x80;
    self.buf_len = self.buf_len + 1;

    if self.buf_len > 112 {
      while self.buf_len < 128 {
        self.buffer[self.buf_len] = 0;
        self.buf_len = self.buf_len + 1;
      }
      self.compress(ref self.buffer);
      self.buf_len = 0;
    }

    while self.buf_len < 112 {
      self.buffer[self.buf_len] = 0;
      self.buf_len = self.buf_len + 1;
    }

    // 128-bit length: upper 64 bits are 0, lower 64 bits are total_bits (big-endian).
    let j: usize = 0;
    while j < 8 {
      self.buffer[112 + j] = 0;
      j = j + 1;
    }
    self.buffer[120] = ((total_bits >> 56) & 0xFF) as u8;
    self.buffer[121] = ((total_bits >> 48) & 0xFF) as u8;
    self.buffer[122] = ((total_bits >> 40) & 0xFF) as u8;
    self.buffer[123] = ((total_bits >> 32) & 0xFF) as u8;
    self.buffer[124] = ((total_bits >> 24) & 0xFF) as u8;
    self.buffer[125] = ((total_bits >> 16) & 0xFF) as u8;
    self.buffer[126] = ((total_bits >> 8) & 0xFF) as u8;
    self.buffer[127] = (total_bits & 0xFF) as u8;

    self.compress(ref self.buffer);

    // Output first 48 bytes (6 of 8 state words, big-endian).
    let result: [u8; 48] = [0u8; 48];
    let w: usize = 0;
    while w < 6 {
      let val = self.state[w];
      result[w * 8 + 0] = ((val >> 56) & 0xFF) as u8;
      result[w * 8 + 1] = ((val >> 48) & 0xFF) as u8;
      result[w * 8 + 2] = ((val >> 40) & 0xFF) as u8;
      result[w * 8 + 3] = ((val >> 32) & 0xFF) as u8;
      result[w * 8 + 4] = ((val >> 24) & 0xFF) as u8;
      result[w * 8 + 5] = ((val >> 16) & 0xFF) as u8;
      result[w * 8 + 6] = ((val >> 8) & 0xFF) as u8;
      result[w * 8 + 7] = (val & 0xFF) as u8;
      w = w + 1;
    }
    return result;
  }

  fn compress(refmut<Self>, block: ref<[u8]>): void {
    // Same compression as SHA-512: prepare message schedule W[0..79].
    let w: [u64; 80] = [0u64; 80];
    let t: usize = 0;
    while t < 16 {
      w[t] = ((block[t * 8] as u64) << 56)
           | ((block[t * 8 + 1] as u64) << 48)
           | ((block[t * 8 + 2] as u64) << 40)
           | ((block[t * 8 + 3] as u64) << 32)
           | ((block[t * 8 + 4] as u64) << 24)
           | ((block[t * 8 + 5] as u64) << 16)
           | ((block[t * 8 + 6] as u64) << 8)
           | (block[t * 8 + 7] as u64);
      t = t + 1;
    }
    while t < 80 {
      let s0 = rotr64(w[t - 15], 1) ^ rotr64(w[t - 15], 8) ^ (w[t - 15] >> 7);
      let s1 = rotr64(w[t - 2], 19) ^ rotr64(w[t - 2], 61) ^ (w[t - 2] >> 6);
      w[t] = w[t - 16] + s0 + w[t - 7] + s1;
      t = t + 1;
    }

    let a = self.state[0]; let b = self.state[1];
    let c = self.state[2]; let d = self.state[3];
    let e = self.state[4]; let f = self.state[5];
    let g = self.state[6]; let h = self.state[7];

    t = 0;
    while t < 80 {
      let S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
      let ch = (e & f) ^ ((~e) & g);
      let temp1 = h + S1 + ch + K[t] + w[t];
      let S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
      let maj = (a & b) ^ (a & c) ^ (b & c);
      let temp2 = S0 + maj;

      h = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
      t = t + 1;
    }

    self.state[0] = self.state[0] + a;
    self.state[1] = self.state[1] + b;
    self.state[2] = self.state[2] + c;
    self.state[3] = self.state[3] + d;
    self.state[4] = self.state[4] + e;
    self.state[5] = self.state[5] + f;
    self.state[6] = self.state[6] + g;
    self.state[7] = self.state[7] + h;
  }
}

/// One-shot SHA-384 convenience function.
function sha384(input: ref<[u8]>): [u8; 48] {
  let h = Sha384::new();
  h.update(input);
  return h.finalize();
}
```

- [ ] **Step 3: Run tests and commit**

Run: `lit test/ --no-progress-bar`
Expected: 243 tests (242 + 1 new), all pass.

```bash
git add std/crypto/sha384.ts test/std/test_sha384.ts
git commit -m "feat: SHA-384 hash function (RFC-0018)"
```

### Task 4: HMAC-SHA512 + verify functions + subtle::eq

**Files:**
- Modify: `std/crypto/hmac.ts` (append after line 48)
- Create: `std/crypto/subtle.ts`

- [ ] **Step 1: Create subtle.ts**

Create `std/crypto/subtle.ts`:

```typescript
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
```

- [ ] **Step 2: Add HMAC-SHA512 and verify functions to hmac.ts**

Append to `std/crypto/hmac.ts`:

```typescript
import { Sha512, sha512 } from './sha512';
import { constant_time_eq } from './subtle';

/// HMAC block size for SHA-512.
const BLOCK_SIZE_512: usize = 128;

/// Compute HMAC-SHA512(key, data) and return a 64-byte MAC.
function hmac_sha512(key: ref<[u8]>, data: ref<[u8]>): [u8; 64] {
  let key_block: [u8; 128] = [0u8; 128];
  if key.len() > BLOCK_SIZE_512 {
    let hashed_key = sha512(key);
    let i: usize = 0;
    while i < 64 {
      key_block[i] = hashed_key[i];
      i = i + 1;
    }
  } else {
    let i: usize = 0;
    while i < key.len() {
      key_block[i] = key[i];
      i = i + 1;
    }
  }

  let ipad: [u8; 128] = [0u8; 128];
  let opad: [u8; 128] = [0u8; 128];
  let i: usize = 0;
  while i < BLOCK_SIZE_512 {
    ipad[i] = key_block[i] ^ 0x36;
    opad[i] = key_block[i] ^ 0x5C;
    i = i + 1;
  }

  let inner = Sha512::new();
  inner.update(ref ipad);
  inner.update(data);
  let inner_hash = inner.finalize();

  let outer = Sha512::new();
  outer.update(ref opad);
  outer.update(ref inner_hash);
  return outer.finalize();
}

/// Verify HMAC-SHA256 using constant-time comparison.
function hmac_sha256_verify(key: ref<[u8]>, data: ref<[u8]>, expected: ref<[u8]>): bool {
  let computed = hmac_sha256(key, data);
  return constant_time_eq(ref computed, expected);
}

/// Verify HMAC-SHA512 using constant-time comparison.
function hmac_sha512_verify(key: ref<[u8]>, data: ref<[u8]>, expected: ref<[u8]>): bool {
  let computed = hmac_sha512(key, data);
  return constant_time_eq(ref computed, expected);
}
```

- [ ] **Step 3: Run tests and commit**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

```bash
git add std/crypto/subtle.ts std/crypto/hmac.ts
git commit -m "feat: HMAC-SHA512 + constant-time verify + subtle::eq (RFC-0018)"
```

### Task 5: UUID v4

**Files:**
- Modify: `std/crypto/random.ts` (append after line 67)

- [ ] **Step 1: Add UUID functions**

Append to `std/crypto/random.ts`:

```typescript
/// Generate a UUID v4 (random) as 16 bytes.
/// Sets version (4) and variant (RFC 4122) bits.
function uuid_v4(): Result<[u8; 16], RandomError> {
  let buf: [u8; 16] = [0u8; 16];
  random_bytes(refmut buf)?;
  // Set version to 4: byte 6 = 0100xxxx.
  buf[6] = (buf[6] & 0x0F) | 0x40;
  // Set variant to RFC 4122: byte 8 = 10xxxxxx.
  buf[8] = (buf[8] & 0x3F) | 0x80;
  return Result::Ok(buf);
}

const HEX_CHARS: [u8; 16] = [
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
];

/// Generate a UUID v4 string: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx".
function uuid_v4_string(): Result<own<String>, RandomError> {
  let bytes = uuid_v4()?;
  let result = String::with_capacity(36);
  // Format: 8-4-4-4-12 hex chars with dashes.
  // Bytes:  0-3, 4-5, 6-7, 8-9, 10-15
  let hex_byte = (b: u8, s: refmut<String>) => {
    s.push(HEX_CHARS[(b >> 4) as usize] as char);
    s.push(HEX_CHARS[(b & 0x0F) as usize] as char);
  };

  let i: usize = 0;
  while i < 4 { hex_byte(bytes[i], &mut result); i = i + 1; }
  result.push('-');
  while i < 6 { hex_byte(bytes[i], &mut result); i = i + 1; }
  result.push('-');
  while i < 8 { hex_byte(bytes[i], &mut result); i = i + 1; }
  result.push('-');
  while i < 10 { hex_byte(bytes[i], &mut result); i = i + 1; }
  result.push('-');
  while i < 16 { hex_byte(bytes[i], &mut result); i = i + 1; }

  return Result::Ok(result);
}
```

- [ ] **Step 2: Run tests and commit**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

```bash
git add std/crypto/random.ts
git commit -m "feat: UUID v4 generation (RFC-0018)"
```

---

## Workstream B: Collections Utilities

### Task 6: Set-like operations (intersect, difference, union)

**Files:**
- Modify: `std/collections/utils.ts` (append after line 95)

- [ ] **Step 1: Add intersect, difference, union**

Append to `std/collections/utils.ts`:

```typescript
/// Elements in both a and b. Preserves order of a. O(n*m).
function intersect<T: PartialEq>(a: ref<Vec<T>>, b: ref<Vec<T>>): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  let i: usize = 0;
  while i < a.len() {
    const item = a.get(i).unwrap();
    let j: usize = 0;
    let found = false;
    while j < b.len() {
      if item.eq(b.get(j).unwrap()) { found = true; break; }
      j = j + 1;
    }
    if found { result.push(item); }
    i = i + 1;
  }
  return result;
}

/// Elements in a that are NOT in b. Preserves order of a. O(n*m).
function difference<T: PartialEq>(a: ref<Vec<T>>, b: ref<Vec<T>>): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  let i: usize = 0;
  while i < a.len() {
    const item = a.get(i).unwrap();
    let j: usize = 0;
    let found = false;
    while j < b.len() {
      if item.eq(b.get(j).unwrap()) { found = true; break; }
      j = j + 1;
    }
    if !found { result.push(item); }
    i = i + 1;
  }
  return result;
}
```

- [ ] **Step 2: Run tests and commit**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

```bash
git add std/collections/utils.ts
git commit -m "feat: intersect/difference set operations (RFC-0017)"
```

### Task 7: Sorting/searching utils (min_index, max_index, bisect)

**Files:**
- Modify: `std/collections/utils.ts` (append)

- [ ] **Step 1: Add min_index, max_index, bisect_left, bisect_right, sort_by_key**

Append to `std/collections/utils.ts`:

```typescript
/// Index of the minimum element. Returns None if empty.
function min_index<T: Ord>(items: ref<Vec<T>>): Option<usize> {
  if items.is_empty() { return Option::None; }
  let best: usize = 0;
  let i: usize = 1;
  while i < items.len() {
    match items.get(i).unwrap().cmp(items.get(best).unwrap()) {
      Ordering::Less => { best = i; },
      _ => {},
    }
    i = i + 1;
  }
  return Option::Some(best);
}

/// Index of the maximum element. Returns None if empty.
function max_index<T: Ord>(items: ref<Vec<T>>): Option<usize> {
  if items.is_empty() { return Option::None; }
  let best: usize = 0;
  let i: usize = 1;
  while i < items.len() {
    match items.get(i).unwrap().cmp(items.get(best).unwrap()) {
      Ordering::Greater => { best = i; },
      _ => {},
    }
    i = i + 1;
  }
  return Option::Some(best);
}

/// Binary search for leftmost insertion point (like Python bisect_left).
/// Items must be sorted. Returns index where value should be inserted to maintain order.
function bisect_left<T: Ord>(items: ref<Vec<T>>, value: ref<T>): usize {
  let lo: usize = 0;
  let hi: usize = items.len();
  while lo < hi {
    let mid = lo + (hi - lo) / 2;
    match items.get(mid).unwrap().cmp(value) {
      Ordering::Less => { lo = mid + 1; },
      _ => { hi = mid; },
    }
  }
  return lo;
}

/// Binary search for rightmost insertion point (like Python bisect_right).
function bisect_right<T: Ord>(items: ref<Vec<T>>, value: ref<T>): usize {
  let lo: usize = 0;
  let hi: usize = items.len();
  while lo < hi {
    let mid = lo + (hi - lo) / 2;
    match items.get(mid).unwrap().cmp(value) {
      Ordering::Greater => { hi = mid; },
      _ => { lo = mid + 1; },
    }
  }
  return lo;
}

/// Sort a Vec in place by a key extracted from each element.
function sort_by_key<T, K: Ord>(items: refmut<Vec<T>>, key_fn: (ref<T>) -> K): void {
  if items.len() <= 1 { return; }
  let i: usize = 1;
  const elem_size = size_of!<T>();
  while i < items.len() {
    let j: usize = i;
    while j > 0 {
      const curr = items.get(j).unwrap();
      const prev = items.get(j - 1).unwrap();
      let curr_key = key_fn(curr);
      let prev_key = key_fn(prev);
      match curr_key.cmp(&prev_key) {
        Ordering::Less => {
          // Swap using raw byte swap.
          let a = (items.ptr as usize + j * elem_size) as *mut u8;
          let b = (items.ptr as usize + (j - 1) * elem_size) as *mut u8;
          let k: usize = 0;
          while k < elem_size {
            const tmp = a[k];
            a[k] = b[k];
            b[k] = tmp;
            k = k + 1;
          }
          j = j - 1;
        },
        _ => { break; },
      }
    }
    i = i + 1;
  }
}
```

- [ ] **Step 2: Run tests and commit**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

```bash
git add std/collections/utils.ts
git commit -m "feat: min_index/max_index/bisect_left/bisect_right/sort_by_key (RFC-0017)"
```

### Task 8: String + counting + combining utils

**Files:**
- Modify: `std/collections/utils.ts` (append)
- Create: `test/std/test_collections_utils.ts`

- [ ] **Step 1: Add remaining utility functions**

Append to `std/collections/utils.ts`:

```typescript
/// Join string slices with a separator. Returns owned String.
function join_ref(parts: ref<Vec<ref<str>>>, sep: ref<str>): own<String> {
  let result = String::new();
  let i: usize = 0;
  while i < parts.len() {
    if i > 0 { result.push_str(sep); }
    result.push_str(*parts.get(i).unwrap());
    i = i + 1;
  }
  return result;
}

/// Remove consecutive duplicate elements in place.
function dedup<T: PartialEq>(items: refmut<Vec<T>>): void {
  if items.len() <= 1 { return; }
  let write: usize = 1;
  let read: usize = 1;
  const elem_size = size_of!<T>();
  while read < items.len() {
    const curr = items.get(read).unwrap();
    const prev = items.get(write - 1).unwrap();
    if !curr.eq(prev) {
      if write != read {
        let dst = (items.ptr as usize + write * elem_size) as *mut u8;
        let src = (items.ptr as usize + read * elem_size) as *const u8;
        memcpy(dst, src, elem_size);
      }
      write = write + 1;
    }
    read = read + 1;
  }
  items.truncate(write);
}

/// Interleave two Vecs: [a0, b0, a1, b1, ...]. Remaining elements appended.
function interleave<T>(a: own<Vec<T>>, b: own<Vec<T>>): own<Vec<T>> {
  let result = Vec::new();
  let ai: usize = 0;
  let bi: usize = 0;
  while ai < a.len() || bi < b.len() {
    if ai < a.len() {
      const elem_size = size_of!<T>();
      const slot = (a.ptr as usize + ai * elem_size) as *const T;
      result.push(unsafe { ptr_read(slot) });
      ai = ai + 1;
    }
    if bi < b.len() {
      const elem_size = size_of!<T>();
      const slot = (b.ptr as usize + bi * elem_size) as *const T;
      result.push(unsafe { ptr_read(slot) });
      bi = bi + 1;
    }
  }
  // Don't drop a/b elements — they were moved out.
  free(a.ptr);
  free(b.ptr);
  return result;
}
```

- [ ] **Step 2: Create test file**

Create `test/std/test_collections_utils.ts`:

```typescript
// RUN: %asc check %s
// Test: Collection utility functions.
function main(): i32 {
  // intersect.
  let a: Vec<i32> = Vec::new();
  a.push(1); a.push(2); a.push(3); a.push(4);
  let b: Vec<i32> = Vec::new();
  b.push(2); b.push(4); b.push(6);
  const inter = intersect(&a, &b);
  assert_eq!(inter.len(), 2);

  // difference.
  const diff = difference(&a, &b);
  assert_eq!(diff.len(), 2);

  // min_index / max_index.
  let v: Vec<i32> = Vec::new();
  v.push(3); v.push(1); v.push(4); v.push(1); v.push(5);
  assert_eq!(min_index(&v).unwrap(), 1);
  assert_eq!(max_index(&v).unwrap(), 4);

  // bisect on sorted vec.
  let sorted: Vec<i32> = Vec::new();
  sorted.push(1); sorted.push(3); sorted.push(5); sorted.push(7);
  assert_eq!(bisect_left(&sorted, &3), 1);
  assert_eq!(bisect_right(&sorted, &3), 2);
  assert_eq!(bisect_left(&sorted, &4), 2);

  // dedup.
  let dup: Vec<i32> = Vec::new();
  dup.push(1); dup.push(1); dup.push(2); dup.push(3); dup.push(3);
  dedup(&dup);
  assert_eq!(dup.len(), 3);

  return 0;
}
```

- [ ] **Step 3: Run tests and commit**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass (243 + 1 new = 244).

```bash
git add std/collections/utils.ts test/std/test_collections_utils.ts
git commit -m "feat: join_ref/dedup/interleave + collection utils test (RFC-0017)"
```

### Task 9: Final Validation

- [ ] **Step 1: Run full test suite**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

- [ ] **Step 2: Verify clean state**

Run: `git status`
Expected: Clean working tree (only untracked temp files in test/e2e/Output/).

- [ ] **Step 3: Count totals**

Run: `wc -l std/encoding/*.ts std/crypto/*.ts std/collections/utils.ts`
Record the new line counts for the audit.
