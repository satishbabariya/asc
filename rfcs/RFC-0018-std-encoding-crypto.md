# RFC-0018 — Std: Encoding and Crypto

| Field       | Value                                        |
|-------------|----------------------------------------------|
| Status      | Accepted                                     |
| Depends on  | RFC-0011, RFC-0013                           |
| Module path | `std::encoding`, `std::crypto`               |
| Inspired by | Deno `@std/encoding`, Deno `@std/crypto`, Bun `Bun.password` |

## Summary

Two modules:

- **`std::encoding`** — byte-level encoding/decoding: base64, base64url, hex, UTF-16,
  varint. All operate on `ref<[u8]>` / `own<Vec<u8>>`. Zero dependency on the crypto module.
- **`std::crypto`** — cryptographic primitives: hashing (SHA-2 family), HMAC,
  password hashing (Argon2, bcrypt), CSPRNG. On Wasm, backed by the host's
  `crypto.getRandomValues()` and WASI crypto proposal where available; falls back to
  a pure-Wasm implementation otherwise.

---

## 1. `std::encoding`

### 1.1 Base64

```typescript
// Standard base64 (RFC 4648 §4). Padding with '=' by default.
namespace base64 {
  function encode(input: ref<[u8]>): own<String>;
  function encode_no_pad(input: ref<[u8]>): own<String>;

  function decode(input: ref<str>): Result<own<Vec<u8>>, Base64Error>;
  function decode_no_pad(input: ref<str>): Result<own<Vec<u8>>, Base64Error>;

  // Encode directly into a pre-allocated buffer (zero extra allocation).
  // Returns number of bytes written. Buffer must be >= encoded_len(input.len()) bytes.
  function encode_into(input: ref<[u8]>, out: refmut<[u8]>): usize;
  function decode_into(input: ref<str>, out: refmut<[u8]>): Result<usize, Base64Error>;

  // Compute the exact encoded/decoded length without encoding.
  function encoded_len(input_len: usize): usize;
  function decoded_len(encoded_len: usize): usize;
}

// URL-safe base64 (RFC 4648 §5): '+' → '-', '/' → '_', no padding by default.
namespace base64url {
  function encode(input: ref<[u8]>): own<String>;
  function decode(input: ref<str>): Result<own<Vec<u8>>, Base64Error>;
  // ... same as base64 namespace
}
```

### 1.2 Hex

```typescript
namespace hex {
  // Encode bytes as lowercase hex: [0xDE, 0xAD] → "dead"
  function encode(input: ref<[u8]>): own<String>;
  // Encode as uppercase hex: [0xDE, 0xAD] → "DEAD"
  function encode_upper(input: ref<[u8]>): own<String>;

  // Decode hex string → bytes. Error on odd length or invalid chars.
  function decode(input: ref<str>): Result<own<Vec<u8>>, HexError>;

  // Encode into pre-allocated buffer (output must be >= input.len() * 2 bytes).
  function encode_into(input: ref<[u8]>, out: refmut<[u8]>): void;
  function decode_into(input: ref<str>, out: refmut<[u8]>): Result<void, HexError>;
}
```

### 1.3 UTF-16

Essential for Wasm/JS interop since JavaScript strings are UTF-16 internally.

```typescript
namespace utf16 {
  // Encode a Rust str (UTF-8) to UTF-16 code units (little-endian). Allocates.
  function encode(input: ref<str>): own<Vec<u16>>;
  // Encode to big-endian UTF-16.
  function encode_be(input: ref<str>): own<Vec<u16>>;

  // Decode UTF-16 code units (LE) to a UTF-8 String.
  // Handles surrogate pairs. Error on lone surrogates.
  function decode(input: ref<[u16]>): Result<own<String>, Utf16Error>;
  function decode_be(input: ref<[u16]>): Result<own<String>, Utf16Error>;

  // Decode from raw bytes (u8 slice treated as LE u16 sequence).
  function decode_bytes(input: ref<[u8]>): Result<own<String>, Utf16Error>;

  // Count UTF-16 code units needed to represent input (no allocation).
  function encoded_len(input: ref<str>): usize;
}
```

### 1.4 Varint (unsigned LEB128)

Used in WASM binary format, Protocol Buffers, and many binary serialization formats.

```typescript
namespace varint {
  // Encode u64 as unsigned LEB128. Returns bytes written (1–10).
  function encode_u64(value: u64, out: refmut<[u8]>): usize;
  function encode_i64(value: i64, out: refmut<[u8]>): usize;  // signed LEB128

  // Decode from bytes. Returns (value, bytes_consumed).
  function decode_u64(input: ref<[u8]>): Result<(u64, usize), VarintError>;
  function decode_i64(input: ref<[u8]>): Result<(i64, usize), VarintError>;

  // Maximum bytes a varint can occupy.
  const MAX_LEN_U32: usize = 5;
  const MAX_LEN_U64: usize = 10;
}
```

### 1.5 Percent-encoding (URL encoding)

```typescript
namespace percent {
  // Encode a string using percent-encoding (RFC 3986).
  // Encodes all chars except unreserved: A-Z a-z 0-9 - _ . ~
  function encode(input: ref<str>): own<String>;

  // Encode, but also preserve the given set of chars unencoded.
  function encode_except(input: ref<str>, safe_chars: ref<str>): own<String>;

  // Decode %XX sequences. Error on invalid sequences.
  function decode(input: ref<str>): Result<own<String>, PercentError>;

  // Decode in place, writing result over the same buffer.
  function decode_in_place(s: refmut<String>): Result<void, PercentError>;
}
```

### 1.6 Error types

```typescript
enum Base64Error { InvalidChar { pos: usize, ch: char }, InvalidPadding, InvalidLength }
enum HexError    { InvalidChar { pos: usize, ch: char }, OddLength }
enum Utf16Error  { LoneSurrogate { pos: usize }, UnpairedSurrogate }
enum VarintError { Overflow, UnexpectedEnd }
enum PercentError { InvalidSequence { pos: usize } }
```

---

## 2. `std::crypto`

### 2.1 Hashing (SHA-2 family)

All hash functions operate in two modes: one-shot (full input at once) and streaming
(update with chunks). Streaming mode uses a `Hasher` struct that is explicitly dropped
to finalize.

```typescript
// One-shot — most common usage
namespace sha256 {
  function hash(input: ref<[u8]>): [u8; 32];         // returns fixed-size array (@copy)
  function hash_str(input: ref<str>): [u8; 32];
  function hash_hex(input: ref<[u8]>): own<String>;  // hex-encoded digest
}

namespace sha512 {
  function hash(input: ref<[u8]>): [u8; 64];
  function hash_str(input: ref<str>): [u8; 64];
  function hash_hex(input: ref<[u8]>): own<String>;
}

namespace sha384 {
  function hash(input: ref<[u8]>): [u8; 48];
  function hash_str(input: ref<str>): [u8; 48];
}

// SHA-3 family
namespace sha3_256 {
  function hash(input: ref<[u8]>): [u8; 32];
}

namespace sha3_512 {
  function hash(input: ref<[u8]>): [u8; 64];
}

// Streaming interface (all hash algorithms implement this pattern)
class Sha256Hasher {
  static new(): own<Sha256Hasher>;
  fn update(refmut<Sha256Hasher>, data: ref<[u8]>): void;
  fn finalize(own<Sha256Hasher>): [u8; 32];    // consumes hasher
  fn finalize_reset(refmut<Sha256Hasher>): [u8; 32]; // resets for reuse
}
// Sha512Hasher, Sha384Hasher, Sha3_256Hasher follow the same pattern.
```

### 2.2 HMAC

```typescript
namespace hmac {
  // HMAC-SHA256
  function sha256(key: ref<[u8]>, data: ref<[u8]>): [u8; 32];
  function sha256_verify(key: ref<[u8]>, data: ref<[u8]>, expected: ref<[u8; 32]>): bool;
    // constant-time comparison to prevent timing attacks

  // HMAC-SHA512
  function sha512(key: ref<[u8]>, data: ref<[u8]>): [u8; 64];
  function sha512_verify(key: ref<[u8]>, data: ref<[u8]>, expected: ref<[u8; 64]>): bool;
}

// Streaming HMAC
class HmacSha256 {
  static new(key: ref<[u8]>): own<HmacSha256>;
  fn update(refmut<HmacSha256>, data: ref<[u8]>): void;
  fn finalize(own<HmacSha256>): [u8; 32];
}
```

### 2.3 Password hashing

Password hashing requires slow, memory-hard algorithms. Two are provided:

**Argon2id** (recommended — winner of Password Hashing Competition):

```typescript
namespace argon2 {
  // Parameters for Argon2id
  @copy
  struct Params {
    memory_kib: u32,    // memory usage in KiB. Default: 65536 (64 MiB)
    iterations: u32,    // time cost. Default: 3
    parallelism: u32,   // parallelism factor. Default: 1 (single-threaded Wasm)
    output_len: usize,  // output hash length in bytes. Default: 32
  }

  const DEFAULT_PARAMS: Params = Params {
    memory_kib: 65536,
    iterations: 3,
    parallelism: 1,
    output_len: 32,
  };

  // Hash a password. Returns a PHC-format string (includes params + salt).
  // e.g. "$argon2id$v=19$m=65536,t=3,p=1$<salt>$<hash>"
  // Generates a random salt internally.
  function hash(password: ref<str>): own<String>;
  function hash_with_params(password: ref<str>, params: Params): own<String>;

  // Verify a password against a PHC string. Constant-time.
  function verify(password: ref<str>, phc_hash: ref<str>): Result<bool, ArgonError>;
}
```

**bcrypt** (widely deployed — for compatibility with existing systems):

```typescript
namespace bcrypt {
  // Cost factor: 4–31. Default: 12. Each increment doubles computation time.
  function hash(password: ref<str>, cost: u32): Result<own<String>, BcryptError>;
  function hash_default(password: ref<str>): Result<own<String>, BcryptError>;
    // cost = 12

  // Verify. Returns Ok(true) on match, Ok(false) on mismatch, Err on malformed hash.
  function verify(password: ref<str>, hash: ref<str>): Result<bool, BcryptError>;

  // Parse cost factor from an existing hash string.
  function cost(hash: ref<str>): Result<u32, BcryptError>;
}
```

### 2.4 Constant-time comparison

To prevent timing attacks when comparing secrets:

```typescript
namespace subtle {
  // Constant-time byte slice equality. Always takes the same time
  // regardless of where the first mismatch occurs.
  function eq(a: ref<[u8]>, b: ref<[u8]>): bool;
}
```

### 2.5 Cryptographically secure random bytes

```typescript
namespace random {
  // Fill buffer with cryptographically random bytes.
  // On Wasm: calls crypto.getRandomValues() via WASI.
  // On native: reads from OS entropy source (/dev/urandom, getrandom syscall).
  function fill(buf: refmut<[u8]>): void;

  // Generate a random value of a specific type.
  function u32(): u32;
  function u64(): u64;
  function bytes(n: usize): own<Vec<u8>>;

  // Generate a UUID v4 (random).
  function uuid_v4(): [u8; 16];
  function uuid_v4_string(): own<String>;  // formatted: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
}
```

### 2.6 Error types

```typescript
enum ArgonError {
  InvalidHash,
  InvalidParams { message: own<String> },
  AllocationFailed,
}

enum BcryptError {
  InvalidHash,
  InvalidCost { cost: u32 },
  PasswordTooLong,  // bcrypt truncates at 72 bytes — error if password > 72 bytes
}
```

---

## 3. Design decisions

| Decision | Choice | Rationale |
|---|---|---|
| SHA hash return types | Fixed-size arrays `[u8; 32]` (`@copy`) | Avoids heap allocation for digests; size known at compile time |
| HMAC comparison | `_verify()` separate from comparison | Forces constant-time comparison; prevents `==` timing vulnerability |
| Argon2 over scrypt | Argon2id as recommended default | PHC winner; configurable memory/time trade-off; resistant to GPU/ASIC attacks |
| bcrypt included | Yes, for compatibility | Many existing systems use bcrypt; migration path to Argon2 |
| bcrypt 72-byte limit | Error (not silent truncation) | Silent truncation is a security bug — fail loudly |
| UTF-16 in encoding | Yes | Wasm/JS interop requires it; JS strings are UTF-16 |
| `subtle::eq` | In `std::crypto` not `std::encoding` | It's a security primitive, not a text utility |
| CSPRNG implementation | Host API first, pure-Wasm fallback | Host entropy is always stronger than user-space PRNG |

---

## 4. Module layout

```
std::encoding
├── base64::{encode, decode, encode_no_pad, decode_no_pad, encode_into, decode_into}
├── base64url::{encode, decode, ...}
├── hex::{encode, encode_upper, decode, encode_into, decode_into}
├── utf16::{encode, encode_be, decode, decode_be, decode_bytes, encoded_len}
├── varint::{encode_u64, encode_i64, decode_u64, decode_i64}
├── percent::{encode, encode_except, decode, decode_in_place}
└── errors::{Base64Error, HexError, Utf16Error, VarintError, PercentError}

std::crypto
├── sha256::{hash, hash_str, hash_hex}  + Sha256Hasher
├── sha512::{hash, hash_str, hash_hex}  + Sha512Hasher
├── sha384::{hash, hash_str}            + Sha384Hasher
├── sha3_256::{hash}                    + Sha3_256Hasher
├── sha3_512::{hash}                    + Sha3_512Hasher
├── hmac::{sha256, sha256_verify, sha512, sha512_verify} + HmacSha256
├── argon2::{hash, hash_with_params, verify, Params, DEFAULT_PARAMS}
├── bcrypt::{hash, hash_default, verify, cost}
├── subtle::{eq}
├── random::{fill, u32, u64, bytes, uuid_v4, uuid_v4_string}
└── errors::{ArgonError, BcryptError}
```

Import pattern:

```typescript
import { sha256, hmac } from 'std/crypto';
import { argon2, bcrypt } from 'std/crypto';
import { base64, hex, utf16 } from 'std/encoding';
```
