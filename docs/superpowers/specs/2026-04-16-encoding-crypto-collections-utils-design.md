# RFC-0018 Encoding/Crypto + RFC-0017 Collections Utils — Depth Push

| Field | Value |
|---|---|
| Date | 2026-04-16 |
| Goal | Push RFC-0018 from 52%→~78% and RFC-0017 from 15%→~50% |
| Baseline | 242/242 tests passing, ~72% weighted RFC coverage |
| Target | RFC-0018: ~78%, RFC-0017: ~50%, overall: ~75% |

## Motivation

RFC-0018 (Encoding/Crypto) and RFC-0017 (Collections Utilities) are the two lowest-coverage RFCs that can be advanced with pure std library work — no compiler changes needed. Both have solid foundations already in place (1,314 LOC across existing files), and the remaining gaps are well-defined additions.

## RFC-0018: Encoding/Crypto Additions

### Encoding Fill-ins

**File: `std/encoding/base64.ts`**
- `encode_no_pad(input: ref<[u8]>): own<String>` — same as encode but strip trailing `=`
- Add `base64url` functions at bottom of same file (or new file `std/encoding/base64url.ts`):
  - `encode(input: ref<[u8]>): own<String>` — use `-` instead of `+`, `_` instead of `/`, no padding
  - `decode(input: ref<str>): Result<own<Vec<u8>>, DecodeError>` — reverse mapping

**File: `std/encoding/hex.ts`**
- `encode_upper(input: ref<[u8]>): own<String>` — same as encode but `ABCDEF` instead of `abcdef`

**File: `std/encoding/varint.ts`**
- `encode_i64(buf: refmut<Vec<u8>>, value: i64): void` — zigzag encode then unsigned LEB128
- `decode_i64(input: ref<[u8]>): Result<(i64, usize), VarintError>` — unsigned LEB128 then zigzag decode

**File: `std/encoding/percent.ts`**
- `encode_except(input: ref<str>, safe: ref<str>): own<String>` — encode all except unreserved + chars in `safe`

### Crypto Additions

**File: `std/crypto/sha384.ts` (new)**
SHA-384 is SHA-512 with different initial hash values and truncated to 48 bytes. Reuse the Sha512 compress logic.
- `Sha384` struct: same fields as Sha512
- `new()`, `update()`, `finalize()` — same as Sha512 but different H0-H7 constants, output truncated to first 48 bytes
- `sha384(input: ref<[u8]>): [u8; 48]` — one-shot convenience function

**File: `std/crypto/hmac.ts` (modify)**
- `hmac_sha512(key: ref<[u8]>, data: ref<[u8]>): [u8; 64]` — same ipad/opad pattern as hmac_sha256, using sha512
- `hmac_sha256_verify(key: ref<[u8]>, data: ref<[u8]>, expected: ref<[u8]>): bool` — compute HMAC then constant-time compare
- `hmac_sha512_verify(key: ref<[u8]>, data: ref<[u8]>, expected: ref<[u8]>): bool` — same pattern

**File: `std/crypto/subtle.ts` (new)**
- `constant_time_eq(a: ref<[u8]>, b: ref<[u8]>): bool` — XOR accumulator, no early exit

**File: `std/crypto/random.ts` (modify)**
- `uuid_v4(): [u8; 16]` — 16 random bytes with version/variant bits set
- `uuid_v4_string(): own<String>` — format as `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx`

## RFC-0017: Collections Utilities Additions

All additions go in `std/collections/utils.ts`.

### Set-like Operations
- `intersect<T: PartialEq>(a: ref<Vec<T>>, b: ref<Vec<T>>): own<Vec<ref<T>>>` — elements in both a and b
- `difference<T: PartialEq>(a: ref<Vec<T>>, b: ref<Vec<T>>): own<Vec<ref<T>>>` — elements in a not in b
- `union<T: PartialEq + Clone>(a: ref<Vec<T>>, b: ref<Vec<T>>): own<Vec<T>>` — all unique elements

### Grouping
- `group_by<T, K: Hash + Eq>(items: ref<Vec<T>>, key_fn: (ref<T>) -> K): own<HashMap<K, Vec<ref<T>>>>` — group elements by key

### Sorting/Searching
- `min_index<T: Ord>(items: ref<Vec<T>>): Option<usize>` — index of minimum element
- `max_index<T: Ord>(items: ref<Vec<T>>): Option<usize>` — index of maximum element
- `bisect_left<T: Ord>(items: ref<Vec<T>>, value: ref<T>): usize` — insertion point (left)
- `bisect_right<T: Ord>(items: ref<Vec<T>>, value: ref<T>): usize` — insertion point (right)
- `sort_by_key<T, K: Ord>(items: refmut<Vec<T>>, key_fn: (ref<T>) -> K): void` — sort in place by key

### String Utilities
- `join_ref(parts: ref<Vec<ref<str>>>, sep: ref<str>): own<String>` — join string slices

### Counting/Dedup
- `frequencies<T: Hash + Eq>(items: ref<Vec<T>>): own<HashMap<ref<T>, usize>>` — count occurrences
- `dedup<T: PartialEq>(items: refmut<Vec<T>>): void` — remove consecutive duplicates

### Combining
- `unzip<A, B>(pairs: own<Vec<(A, B)>>): (own<Vec<A>>, own<Vec<B>>)` — split pairs
- `interleave<T>(a: own<Vec<T>>, b: own<Vec<T>>): own<Vec<T>>` — alternate elements

## What This Does NOT Include

- SHA-3 (Keccak sponge — ~300 LOC of complex permutation logic)
- bcrypt (Blowfish cipher — ~400 LOC)
- Streaming HmacSha256 class
- combinations/permutations (complex recursive generation)
- shuffle/sample/Rng trait (needs trait infrastructure)
- cycle_n/scan_sum/tap (need `impl Trait` return types)
- deep_merge (needs JsonValue type)

## Files Modified/Created

**New files:**
- `std/crypto/sha384.ts` (~120 LOC)
- `std/crypto/subtle.ts` (~20 LOC)
- `test/std/test_sha384.ts`
- `test/std/test_collections_utils.ts`

**Modified files:**
- `std/encoding/base64.ts` — add encode_no_pad, base64url encode/decode
- `std/encoding/hex.ts` — add encode_upper
- `std/encoding/varint.ts` — add encode_i64/decode_i64
- `std/encoding/percent.ts` — add encode_except
- `std/crypto/hmac.ts` — add hmac_sha512, verify functions
- `std/crypto/random.ts` — add uuid_v4, uuid_v4_string
- `std/collections/utils.ts` — add 12 utility functions

## Estimated LOC

| Area | LOC |
|------|-----|
| Encoding fill-ins | ~120 |
| SHA-384 | ~120 |
| HMAC additions | ~60 |
| subtle + uuid | ~60 |
| Collection utils | ~350 |
| Tests | ~150 |
| **Total** | **~860** |
