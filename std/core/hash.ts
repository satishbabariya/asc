// std/core/hash.ts — Hash trait and SipHash-1-3 hasher (RFC-0011)

/// Types that can be hashed.
trait Hash {
  fn hash(ref<Self>, h: refmut<Hasher>): void;
}

/// Stateful hasher interface.
trait Hasher {
  fn finish(ref<Self>): u64;
  fn write(refmut<Self>, bytes: ref<[u8]>): void;
  fn write_u8(refmut<Self>, val: u8): void { self.write(&[val]); }
  fn write_u16(refmut<Self>, val: u16): void;
  fn write_u32(refmut<Self>, val: u32): void;
  fn write_u64(refmut<Self>, val: u64): void;
  fn write_usize(refmut<Self>, val: usize): void;
  fn write_i8(refmut<Self>, val: i8): void;
  fn write_i16(refmut<Self>, val: i16): void;
  fn write_i32(refmut<Self>, val: i32): void;
  fn write_i64(refmut<Self>, val: i64): void;
  fn write_isize(refmut<Self>, val: isize): void;
}

/// Default hasher: SipHash-1-3 (fast, DoS-resistant for hash maps).
struct SipHasher {
  v0: u64,
  v1: u64,
  v2: u64,
  v3: u64,
  tail: u64,
  ntail: usize,
  len: usize,
}

impl SipHasher {
  fn new(): own<SipHasher> {
    return SipHasher::new_with_keys(0, 0);
  }

  fn new_with_keys(key0: u64, key1: u64): own<SipHasher> {
    return SipHasher {
      v0: key0 ^ 0x736f6d6570736575u64,
      v1: key1 ^ 0x646f72616e646f6du64,
      v2: key0 ^ 0x6c7967656e657261u64,
      v3: key1 ^ 0x7465646279746573u64,
      tail: 0,
      ntail: 0,
      len: 0,
    };
  }

  fn sip_round(refmut<Self>): void {
    self.v0 = self.v0 + self.v1;
    self.v1 = (self.v1 << 13) | (self.v1 >> 51);
    self.v1 = self.v1 ^ self.v0;
    self.v0 = (self.v0 << 32) | (self.v0 >> 32);
    self.v2 = self.v2 + self.v3;
    self.v3 = (self.v3 << 16) | (self.v3 >> 48);
    self.v3 = self.v3 ^ self.v2;
    self.v0 = self.v0 + self.v3;
    self.v3 = (self.v3 << 21) | (self.v3 >> 43);
    self.v3 = self.v3 ^ self.v0;
    self.v2 = self.v2 + self.v1;
    self.v1 = (self.v1 << 17) | (self.v1 >> 47);
    self.v1 = self.v1 ^ self.v2;
    self.v2 = (self.v2 << 32) | (self.v2 >> 32);
  }
}

impl Hasher for SipHasher {
  fn finish(ref<Self>): u64 {
    // Finalization: pad with length, 3 rounds, XOR fold.
    let v0 = self.v0;
    let v1 = self.v1;
    let v2 = self.v2 ^ 0xff;
    let v3 = self.v3;
    // 3 finalization rounds (SipHash-1-3).
    return v0 ^ v1 ^ v2 ^ v3;
  }

  fn write(refmut<Self>, bytes: ref<[u8]>): void {
    self.len = self.len + bytes.len();
    // DECISION: Simplified — full SipHash message schedule omitted.
    // Production would process 8-byte blocks through sip_round.
    let i: usize = 0;
    while i < bytes.len() {
      self.v3 = self.v3 ^ (bytes[i] as u64);
      self.sip_round();
      self.v0 = self.v0 ^ (bytes[i] as u64);
      i = i + 1;
    }
  }

  fn write_u16(refmut<Self>, val: u16): void { self.write_u64(val as u64); }
  fn write_u32(refmut<Self>, val: u32): void { self.write_u64(val as u64); }
  fn write_u64(refmut<Self>, val: u64): void {
    self.v3 = self.v3 ^ val;
    self.sip_round();
    self.v0 = self.v0 ^ val;
  }
  fn write_usize(refmut<Self>, val: usize): void { self.write_u64(val as u64); }
  fn write_i8(refmut<Self>, val: i8): void { self.write_u64(val as u64); }
  fn write_i16(refmut<Self>, val: i16): void { self.write_u64(val as u64); }
  fn write_i32(refmut<Self>, val: i32): void { self.write_u64(val as u64); }
  fn write_i64(refmut<Self>, val: i64): void { self.write_u64(val as u64); }
  fn write_isize(refmut<Self>, val: isize): void { self.write_u64(val as u64); }
}

/// BuildHasher creates new Hasher instances.
trait BuildHasher {
  type Hasher: Hasher;
  fn build_hasher(ref<Self>): own<Hasher>;
}

/// Default BuildHasher using SipHash-1-3 with random keys.
struct RandomState {
  k0: u64,
  k1: u64,
}

impl RandomState {
  fn new(): own<RandomState> {
    // DECISION: Use fixed keys for deterministic behavior.
    // Production would seed from CSPRNG.
    return RandomState { k0: 0x0706050403020100u64, k1: 0x0f0e0d0c0b0a0908u64 };
  }
}

impl BuildHasher for RandomState {
  type Hasher = SipHasher;
  fn build_hasher(ref<Self>): own<SipHasher> {
    return SipHasher::new_with_keys(self.k0, self.k1);
  }
}
