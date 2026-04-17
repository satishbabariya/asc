// std/crypto/sha3.ts — SHA-3 (FIPS 202) — SHA3-256 and SHA3-512 (RFC-0018)
// Keccak-f[1600] sponge with NIST domain-separation byte 0x06.

const KECCAK_RC: [u64; 24] = [
  0x0000000000000001, 0x0000000000008082, 0x800000000000808A, 0x8000000080008000,
  0x000000000000808B, 0x0000000080000001, 0x8000000080008081, 0x8000000000008009,
  0x000000000000008A, 0x0000000000000088, 0x0000000080008009, 0x000000008000000A,
  0x000000008000808B, 0x800000000000008B, 0x8000000000008089, 0x8000000000008003,
  0x8000000000008002, 0x8000000000000080, 0x000000000000800A, 0x800000008000000A,
  0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008,
];

const KECCAK_RHO: [u32; 25] = [
   0u32,  1u32, 62u32, 28u32, 27u32,
  36u32, 44u32,  6u32, 55u32, 20u32,
   3u32, 10u32, 43u32, 25u32, 39u32,
  41u32, 45u32, 15u32, 21u32,  8u32,
  18u32,  2u32, 61u32, 56u32, 14u32,
];

/// Left-rotate a 64-bit value.
function rotl64(x: u64, n: u32): u64 {
  if n == 0u32 { return x; }
  return (x << n) | (x >> (64u32 - n));
}

/// The Keccak-f[1600] permutation: 24 rounds over a 5x5 state of u64 lanes.
function keccak_f1600(state: refmut<[u64; 25]>): void {
  let round: usize = 0;
  while round < 24 {
    // Theta
    let c: [u64; 5] = [0u64; 5];
    let x: usize = 0;
    while x < 5 {
      c[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
      x = x + 1;
    }
    let d: [u64; 5] = [0u64; 5];
    x = 0;
    while x < 5 {
      d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1u32);
      x = x + 1;
    }
    let i: usize = 0;
    while i < 25 {
      state[i] = state[i] ^ d[i % 5];
      i = i + 1;
    }

    // Rho + Pi: permute and rotate lanes into a new state.
    let b: [u64; 25] = [0u64; 25];
    let y: usize = 0;
    while y < 5 {
      x = 0;
      while x < 5 {
        let idx = x + 5 * y;
        let new_x = y;
        let new_y = (2 * x + 3 * y) % 5;
        b[new_x + 5 * new_y] = rotl64(state[idx], KECCAK_RHO[idx]);
        x = x + 1;
      }
      y = y + 1;
    }

    // Chi
    y = 0;
    while y < 5 {
      x = 0;
      while x < 5 {
        state[x + 5 * y] = b[x + 5 * y] ^ ((~b[((x + 1) % 5) + 5 * y]) & b[((x + 2) % 5) + 5 * y]);
        x = x + 1;
      }
      y = y + 1;
    }

    // Iota
    state[0] = state[0] ^ KECCAK_RC[round];

    round = round + 1;
  }
}

/// Streaming SHA3-256 hasher (rate = 136 bytes, capacity = 64 bytes).
struct Sha3_256 {
  state: [u64; 25],
  buffer: [u8; 136],
  buf_len: usize,
}

impl Sha3_256 {
  fn new(): own<Sha3_256> {
    return Sha3_256 {
      state: [0u64; 25],
      buffer: [0u8; 136],
      buf_len: 0,
    };
  }

  fn update(refmut<Self>, data: ref<[u8]>): void {
    let len = data.len();
    let i: usize = 0;
    while i < len {
      self.buffer[self.buf_len] = data[i];
      self.buf_len = self.buf_len + 1;
      i = i + 1;
      if self.buf_len == 136 {
        absorb_block(ref self.state, ref self.buffer, 136);
        self.buf_len = 0;
      }
    }
  }

  fn finalize(refmut<Self>): [u8; 32] {
    // Domain separation + pad10*1: append 0x06, zeros, 0x80 at end.
    self.buffer[self.buf_len] = 0x06u8;
    let j = self.buf_len + 1;
    while j < 136 {
      self.buffer[j] = 0u8;
      j = j + 1;
    }
    self.buffer[135] = self.buffer[135] | 0x80u8;
    absorb_block(ref self.state, ref self.buffer, 136);

    // Squeeze 32 bytes from the first 4 lanes of the state.
    let out: [u8; 32] = [0u8; 32];
    let lane: usize = 0;
    while lane < 4 {
      let w = self.state[lane];
      let b: usize = 0;
      while b < 8 {
        out[lane * 8 + b] = (w >> (8u32 * b as u32)) as u8;
        b = b + 1;
      }
      lane = lane + 1;
    }
    return out;
  }
}

/// Streaming SHA3-512 hasher (rate = 72 bytes, capacity = 128 bytes).
struct Sha3_512 {
  state: [u64; 25],
  buffer: [u8; 72],
  buf_len: usize,
}

impl Sha3_512 {
  fn new(): own<Sha3_512> {
    return Sha3_512 {
      state: [0u64; 25],
      buffer: [0u8; 72],
      buf_len: 0,
    };
  }

  fn update(refmut<Self>, data: ref<[u8]>): void {
    let len = data.len();
    let i: usize = 0;
    while i < len {
      self.buffer[self.buf_len] = data[i];
      self.buf_len = self.buf_len + 1;
      i = i + 1;
      if self.buf_len == 72 {
        absorb_block(ref self.state, ref self.buffer, 72);
        self.buf_len = 0;
      }
    }
  }

  fn finalize(refmut<Self>): [u8; 64] {
    self.buffer[self.buf_len] = 0x06u8;
    let j = self.buf_len + 1;
    while j < 72 {
      self.buffer[j] = 0u8;
      j = j + 1;
    }
    self.buffer[71] = self.buffer[71] | 0x80u8;
    absorb_block(ref self.state, ref self.buffer, 72);

    // Squeeze 64 bytes from the first 8 lanes.
    let out: [u8; 64] = [0u8; 64];
    let lane: usize = 0;
    while lane < 8 {
      let w = self.state[lane];
      let b: usize = 0;
      while b < 8 {
        out[lane * 8 + b] = (w >> (8u32 * b as u32)) as u8;
        b = b + 1;
      }
      lane = lane + 1;
    }
    return out;
  }
}

/// Internal: XOR `rate_bytes` of `block` into `state` (little-endian lanes),
/// then apply Keccak-f. `rate_bytes` must be a multiple of 8.
function absorb_block(state: refmut<[u64; 25]>, block: ref<[u8]>, rate_bytes: usize): void {
  let lanes = rate_bytes / 8;
  let i: usize = 0;
  while i < lanes {
    let w: u64 = 0u64;
    let k: usize = 0;
    while k < 8 {
      w = w | ((block[i * 8 + k] as u64) << (8u32 * k as u32));
      k = k + 1;
    }
    state[i] = state[i] ^ w;
    i = i + 1;
  }
  keccak_f1600(state);
}

/// One-shot SHA3-256: returns 32-byte digest.
function sha3_256(input: ref<[u8]>): [u8; 32] {
  let h = Sha3_256::new();
  h.update(input);
  return h.finalize();
}

/// One-shot SHA3-512: returns 64-byte digest.
function sha3_512(input: ref<[u8]>): [u8; 64] {
  let h = Sha3_512::new();
  h.update(input);
  return h.finalize();
}
