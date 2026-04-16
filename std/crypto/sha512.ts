// std/crypto/sha512.ts — Pure SHA-512 implementation (RFC-0018)

/// SHA-512 initial hash values (first 64 bits of fractional parts of sqrt of first 8 primes).
const H_INIT: [u64; 8] = [
  0x6a09e667f3bcc908, 0xbb67ae8584caa73b,
  0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
  0x510e527fade682d1, 0x9b05688c2b3e6c1f,
  0x1f83d9abfb41bd6b, 0x5be0cd19137e2179,
];

/// SHA-512 round constants (first 64 bits of fractional parts of cube roots of first 80 primes).
const K: [u64; 80] = [
  0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc,
  0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118,
  0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2,
  0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694,
  0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
  0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5,
  0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4,
  0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70,
  0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df,
  0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
  0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30,
  0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8,
  0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8,
  0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3,
  0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
  0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b,
  0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178,
  0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b,
  0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c,
  0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817,
];

/// Streaming SHA-512 hasher.
struct Sha512 {
  state: [u64; 8],
  buffer: [u8; 128],
  buf_len: usize,
  total_len: u64,
}

impl Sha512 {
  /// Create a new SHA-512 hasher.
  fn new(): own<Sha512> {
    let s = Sha512 {
      state: H_INIT,
      buffer: [0u8; 128],
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
      let fill = 128 - self.buf_len;
      if fill > len { fill = len; }
      let j: usize = 0;
      while j < fill {
        self.buffer[self.buf_len + j] = data[j];
        j = j + 1;
      }
      self.buf_len = self.buf_len + fill;
      i = fill;
      if self.buf_len == 128 {
        self.compress(ref self.buffer);
        self.buf_len = 0;
      }
    }

    // Process full 128-byte blocks.
    while i + 128 <= len {
      let block = data.slice(i, i + 128);
      self.compress(block);
      i = i + 128;
    }

    // Store remainder.
    while i < len {
      self.buffer[self.buf_len] = data[i];
      self.buf_len = self.buf_len + 1;
      i = i + 1;
    }
  }

  /// Finalize and return the 64-byte hash.
  fn finalize(refmut<Self>): [u8; 64] {
    // Padding.
    let total_bits = self.total_len * 8;
    self.buffer[self.buf_len] = 0x80;
    self.buf_len = self.buf_len + 1;

    if self.buf_len > 112 {
      // Not enough room for 16-byte length — fill and compress, then new block.
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

    // SHA-512 uses a 128-bit length field. Upper 64 bits are zero for our use.
    self.buffer[112] = 0;
    self.buffer[113] = 0;
    self.buffer[114] = 0;
    self.buffer[115] = 0;
    self.buffer[116] = 0;
    self.buffer[117] = 0;
    self.buffer[118] = 0;
    self.buffer[119] = 0;

    // Lower 64 bits: total length in bits as big-endian u64.
    self.buffer[120] = (total_bits >> 56) as u8;
    self.buffer[121] = (total_bits >> 48) as u8;
    self.buffer[122] = (total_bits >> 40) as u8;
    self.buffer[123] = (total_bits >> 32) as u8;
    self.buffer[124] = (total_bits >> 24) as u8;
    self.buffer[125] = (total_bits >> 16) as u8;
    self.buffer[126] = (total_bits >> 8) as u8;
    self.buffer[127] = total_bits as u8;
    self.compress(ref self.buffer);

    // Produce 64-byte output.
    let hash: [u8; 64] = [0u8; 64];
    let i: usize = 0;
    while i < 8 {
      let w = self.state[i];
      hash[i * 8] = (w >> 56) as u8;
      hash[i * 8 + 1] = (w >> 48) as u8;
      hash[i * 8 + 2] = (w >> 40) as u8;
      hash[i * 8 + 3] = (w >> 32) as u8;
      hash[i * 8 + 4] = (w >> 24) as u8;
      hash[i * 8 + 5] = (w >> 16) as u8;
      hash[i * 8 + 6] = (w >> 8) as u8;
      hash[i * 8 + 7] = w as u8;
      i = i + 1;
    }
    return hash;
  }

  /// Internal: compress a single 128-byte block.
  fn compress(refmut<Self>, block: ref<[u8]>): void {
    // Prepare message schedule (80 words of 64 bits).
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

    // 80 rounds of compression.
    t = 0;
    while t < 80 {
      let S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
      let ch = (e & f) ^ ((~e) & g);
      let temp1 = h +% S1 +% ch +% K[t] +% w[t];
      let S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
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

/// Right-rotate a 64-bit value.
function rotr64(x: u64, n: u64): u64 {
  return (x >> n) | (x << (64 - n));
}

/// One-shot SHA-512: hash input and return 64-byte digest.
function sha512(input: ref<[u8]>): [u8; 64] {
  let hasher = Sha512::new();
  hasher.update(input);
  return hasher.finalize();
}
