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
