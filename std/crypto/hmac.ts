// std/crypto/hmac.ts — HMAC-SHA256 (RFC-0018)

import { Sha256, sha256 } from './sha256';

/// HMAC block size for SHA-256.
const BLOCK_SIZE: usize = 64;

/// Compute HMAC-SHA256(key, data) and return a 32-byte MAC.
function hmac_sha256(key: ref<[u8]>, data: ref<[u8]>): [u8; 32] {
  // If key is longer than block size, hash it first.
  let key_block: [u8; 64] = [0u8; 64];
  if key.len() > BLOCK_SIZE {
    let hashed_key = sha256(key);
    let i: usize = 0;
    while i < 32 {
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

  // Compute inner and outer padded keys.
  let ipad: [u8; 64] = [0u8; 64];
  let opad: [u8; 64] = [0u8; 64];
  let i: usize = 0;
  while i < BLOCK_SIZE {
    ipad[i] = key_block[i] ^ 0x36;
    opad[i] = key_block[i] ^ 0x5C;
    i = i + 1;
  }

  // Inner hash: SHA256(ipad || data).
  let inner = Sha256::new();
  inner.update(ref ipad);
  inner.update(data);
  let inner_hash = inner.finalize();

  // Outer hash: SHA256(opad || inner_hash).
  let outer = Sha256::new();
  outer.update(ref opad);
  outer.update(ref inner_hash);
  return outer.finalize();
}

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
