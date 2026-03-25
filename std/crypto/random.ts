// std/crypto/random.ts — CSPRNG interface (RFC-0018)
// Uses WASI random_get or a host-provided __asc_random_get extern.

/// External: fill buffer with cryptographically secure random bytes.
/// On wasm32-wasi this maps to random_get; on native targets to OS CSPRNG.
@extern("env", "__asc_random_get")
declare function __asc_random_get(buf: *mut u8, len: usize): i32;

/// Error for random number generation failures.
enum RandomError {
  OsFailed(i32),
}

impl Display for RandomError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      RandomError::OsFailed(code) => f.write_str("random_get failed with code ").and_then(|_| code.fmt(f)),
    }
  }
}

/// Fill a mutable byte slice with cryptographically secure random bytes.
function random_bytes(buf: refmut<[u8]>): Result<void, RandomError> {
  let rc = __asc_random_get(buf.as_mut_ptr(), buf.len());
  if rc != 0 {
    return Result::Err(RandomError::OsFailed(rc));
  }
  return Result::Ok(void);
}

/// Generate a random u32.
function random_u32(): Result<u32, RandomError> {
  let buf: [u8; 4] = [0u8; 4];
  random_bytes(refmut buf)?;
  let val = ((buf[0] as u32) << 24)
          | ((buf[1] as u32) << 16)
          | ((buf[2] as u32) << 8)
          | (buf[3] as u32);
  return Result::Ok(val);
}

/// Generate a random u64.
function random_u64(): Result<u64, RandomError> {
  let buf: [u8; 8] = [0u8; 8];
  random_bytes(refmut buf)?;
  let val = ((buf[0] as u64) << 56)
          | ((buf[1] as u64) << 48)
          | ((buf[2] as u64) << 40)
          | ((buf[3] as u64) << 32)
          | ((buf[4] as u64) << 24)
          | ((buf[5] as u64) << 16)
          | ((buf[6] as u64) << 8)
          | (buf[7] as u64);
  return Result::Ok(val);
}

/// Generate a random u32 in [0, exclusive_upper_bound).
function random_u32_range(upper: u32): Result<u32, RandomError> {
  // Rejection sampling to avoid modulo bias.
  let threshold = (0xFFFFFFFF - upper + 1) % upper;
  loop {
    let val = random_u32()?;
    if val >= threshold {
      return Result::Ok(val % upper);
    }
  }
}
