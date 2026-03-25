// std/crypto/sha256.ts — Pure SHA-256 implementation (RFC-0018)

/// SHA-256 initial hash values (first 32 bits of fractional parts of sqrt of first 8 primes).
const H_INIT: [u32; 8] = [
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
];

/// SHA-256 round constants (first 32 bits of fractional parts of cube roots of first 64 primes).
const K: [u32; 64] = [
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

/// Streaming SHA-256 hasher.
struct Sha256 {
  state: [u32; 8],
  buffer: [u8; 64],
  buf_len: usize,
  total_len: u64,
}

impl Sha256 {
  /// Create a new SHA-256 hasher.
  fn new(): own<Sha256> {
    let s = Sha256 {
      state: H_INIT,
      buffer: [0u8; 64],
      buf_len: 0,
      total_len: 0,
    };
    return s;
  }

  /// Feed data into the hasher.
  fn update(refmut<Self>, data: ref<[u8]>): void {
    let i: usize = 0;
    let len = data.len();
    self.total_len = self.total_len + len as u64;

    // Fill buffer if partially full.
    if self.buf_len > 0 {
      let fill = 64 - self.buf_len;
      if fill > len { fill = len; }
      let j: usize = 0;
      while j < fill {
        self.buffer[self.buf_len + j] = data[j];
        j = j + 1;
      }
      self.buf_len = self.buf_len + fill;
      i = fill;
      if self.buf_len == 64 {
        self.compress(ref self.buffer);
        self.buf_len = 0;
      }
    }

    // Process full blocks.
    while i + 64 <= len {
      let block = data.slice(i, i + 64);
      self.compress(block);
      i = i + 64;
    }

    // Store remainder.
    while i < len {
      self.buffer[self.buf_len] = data[i];
      self.buf_len = self.buf_len + 1;
      i = i + 1;
    }
  }

  /// Finalize and return the 32-byte hash.
  fn finalize(refmut<Self>): [u8; 32] {
    // Padding.
    let total_bits = self.total_len * 8;
    self.buffer[self.buf_len] = 0x80;
    self.buf_len = self.buf_len + 1;

    if self.buf_len > 56 {
      // Not enough room for length — fill and compress, then new block.
      while self.buf_len < 64 {
        self.buffer[self.buf_len] = 0;
        self.buf_len = self.buf_len + 1;
      }
      self.compress(ref self.buffer);
      self.buf_len = 0;
    }

    while self.buf_len < 56 {
      self.buffer[self.buf_len] = 0;
      self.buf_len = self.buf_len + 1;
    }

    // Append total length in bits as big-endian u64.
    self.buffer[56] = (total_bits >> 56) as u8;
    self.buffer[57] = (total_bits >> 48) as u8;
    self.buffer[58] = (total_bits >> 40) as u8;
    self.buffer[59] = (total_bits >> 32) as u8;
    self.buffer[60] = (total_bits >> 24) as u8;
    self.buffer[61] = (total_bits >> 16) as u8;
    self.buffer[62] = (total_bits >> 8) as u8;
    self.buffer[63] = total_bits as u8;
    self.compress(ref self.buffer);

    // Produce output.
    let hash: [u8; 32] = [0u8; 32];
    let i: usize = 0;
    while i < 8 {
      let w = self.state[i];
      hash[i * 4] = (w >> 24) as u8;
      hash[i * 4 + 1] = (w >> 16) as u8;
      hash[i * 4 + 2] = (w >> 8) as u8;
      hash[i * 4 + 3] = w as u8;
      i = i + 1;
    }
    return hash;
  }

  /// Internal: compress a single 64-byte block.
  fn compress(refmut<Self>, block: ref<[u8]>): void {
    // Prepare message schedule.
    let w: [u32; 64] = [0u32; 64];
    let t: usize = 0;
    while t < 16 {
      w[t] = ((block[t * 4] as u32) << 24)
           | ((block[t * 4 + 1] as u32) << 16)
           | ((block[t * 4 + 2] as u32) << 8)
           | (block[t * 4 + 3] as u32);
      t = t + 1;
    }
    while t < 64 {
      let s0 = rotr32(w[t - 15], 7) ^ rotr32(w[t - 15], 18) ^ (w[t - 15] >> 3);
      let s1 = rotr32(w[t - 2], 17) ^ rotr32(w[t - 2], 19) ^ (w[t - 2] >> 10);
      w[t] = w[t - 16] +% s0 +% w[t - 7] +% s1;
      t = t + 1;
    }

    // Working variables.
    let a = self.state[0];
    let b = self.state[1];
    let c = self.state[2];
    let d = self.state[3];
    let e = self.state[4];
    let f = self.state[5];
    let g = self.state[6];
    let h = self.state[7];

    // Compression.
    t = 0;
    while t < 64 {
      let S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      let ch = (e & f) ^ ((~e) & g);
      let temp1 = h +% S1 +% ch +% K[t] +% w[t];
      let S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      let maj = (a & b) ^ (a & c) ^ (b & c);
      let temp2 = S0 +% maj;

      h = g;
      g = f;
      f = e;
      e = d +% temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 +% temp2;
      t = t + 1;
    }

    self.state[0] = self.state[0] +% a;
    self.state[1] = self.state[1] +% b;
    self.state[2] = self.state[2] +% c;
    self.state[3] = self.state[3] +% d;
    self.state[4] = self.state[4] +% e;
    self.state[5] = self.state[5] +% f;
    self.state[6] = self.state[6] +% g;
    self.state[7] = self.state[7] +% h;
  }
}

/// Right-rotate a 32-bit value.
function rotr32(x: u32, n: u32): u32 {
  return (x >> n) | (x << (32 - n));
}

/// One-shot SHA-256: hash input and return 32-byte digest.
function sha256(input: ref<[u8]>): [u8; 32] {
  let hasher = Sha256::new();
  hasher.update(input);
  return hasher.finalize();
}
