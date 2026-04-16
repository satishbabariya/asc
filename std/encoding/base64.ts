// std/encoding/base64.ts — Base64 encode/decode (RFC-0018)

/// Error type for Base64 decoding.
enum DecodeError {
  InvalidCharacter(usize),
  InvalidLength,
  InvalidPadding,
}

impl Display for DecodeError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      DecodeError::InvalidCharacter(pos) => f.write_str("invalid base64 character at ").and_then(|_| pos.fmt(f)),
      DecodeError::InvalidLength => f.write_str("invalid base64 length"),
      DecodeError::InvalidPadding => f.write_str("invalid base64 padding"),
    }
  }
}

/// Standard Base64 alphabet (RFC 4648).
const ENCODE_TABLE: [u8; 64] = [
  // A-Z
  0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
  0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
  // a-z
  0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
  0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
  // 0-9
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  // +, /
  0x2B, 0x2F,
];

/// Encode bytes to a Base64 string.
function encode(input: ref<[u8]>): own<String> {
  let len = input.len();
  let out_len = ((len + 2) / 3) * 4;
  let result = String::with_capacity(out_len);
  let i: usize = 0;

  while i + 2 < len {
    let b0 = input[i] as u32;
    let b1 = input[i + 1] as u32;
    let b2 = input[i + 2] as u32;
    let triple = (b0 << 16) | (b1 << 8) | b2;
    result.push(ENCODE_TABLE[((triple >> 18) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE[((triple >> 12) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE[((triple >> 6) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE[(triple & 0x3F) as usize] as char);
    i = i + 3;
  }

  let remaining = len - i;
  if remaining == 2 {
    let b0 = input[i] as u32;
    let b1 = input[i + 1] as u32;
    let triple = (b0 << 16) | (b1 << 8);
    result.push(ENCODE_TABLE[((triple >> 18) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE[((triple >> 12) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE[((triple >> 6) & 0x3F) as usize] as char);
    result.push('=' as char);
  } else if remaining == 1 {
    let b0 = input[i] as u32;
    let triple = b0 << 16;
    result.push(ENCODE_TABLE[((triple >> 18) & 0x3F) as usize] as char);
    result.push(ENCODE_TABLE[((triple >> 12) & 0x3F) as usize] as char);
    result.push('=' as char);
    result.push('=' as char);
  }

  return result;
}

/// Decode a Base64 string to bytes.
function decode(input: ref<str>): Result<own<Vec<u8>>, DecodeError> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  if len == 0 { return Result::Ok(Vec::new()); }
  if len % 4 != 0 { return Result::Err(DecodeError::InvalidLength); }

  // Count padding.
  let pad: usize = 0;
  if bytes[len - 1] == 0x3D { pad = pad + 1; }
  if bytes[len - 2] == 0x3D { pad = pad + 1; }

  let out_len = (len / 4) * 3 - pad;
  let result: own<Vec<u8>> = Vec::with_capacity(out_len);
  let i: usize = 0;

  while i < len {
    let a = decode_char(bytes[i], i)?;
    let b = decode_char(bytes[i + 1], i + 1)?;
    let c: u8 = 0;
    let d: u8 = 0;
    let have_c = bytes[i + 2] != 0x3D;
    let have_d = bytes[i + 3] != 0x3D;
    if have_c { c = decode_char(bytes[i + 2], i + 2)?; }
    if have_d { d = decode_char(bytes[i + 3], i + 3)?; }

    let triple = ((a as u32) << 18) | ((b as u32) << 12) | ((c as u32) << 6) | (d as u32);
    result.push(((triple >> 16) & 0xFF) as u8);
    if have_c { result.push(((triple >> 8) & 0xFF) as u8); }
    if have_d { result.push((triple & 0xFF) as u8); }
    i = i + 4;
  }

  return Result::Ok(result);
}

/// Decode a single Base64 character to its 6-bit value.
function decode_char(c: u8, pos: usize): Result<u8, DecodeError> {
  if c >= 0x41 && c <= 0x5A { return Result::Ok(c - 0x41); }       // A-Z
  if c >= 0x61 && c <= 0x7A { return Result::Ok(c - 0x61 + 26); }  // a-z
  if c >= 0x30 && c <= 0x39 { return Result::Ok(c - 0x30 + 52); }  // 0-9
  if c == 0x2B { return Result::Ok(62); }                            // +
  if c == 0x2F { return Result::Ok(63); }                            // /
  return Result::Err(DecodeError::InvalidCharacter(pos));
}

/// Encode bytes to Base64 without padding.
function encode_no_pad(input: ref<[u8]>): own<String> {
  let result = encode(input);
  while result.len() > 0 {
    const last_idx = result.len() - 1;
    const last_byte = result.as_bytes()[last_idx];
    if last_byte != 0x3D { break; }
    result.truncate(last_idx);
  }
  return result;
}

/// URL-safe Base64 alphabet: '+' -> '-', '/' -> '_', no padding.
const ENCODE_TABLE_URL: [u8; 64] = [
  0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
  0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
  0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
  0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
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
  if c >= 0x41 && c <= 0x5A { return Result::Ok(c - 0x41); }
  if c >= 0x61 && c <= 0x7A { return Result::Ok(c - 0x61 + 26); }
  if c >= 0x30 && c <= 0x39 { return Result::Ok(c - 0x30 + 52); }
  if c == 0x2D { return Result::Ok(62); }
  if c == 0x5F { return Result::Ok(63); }
  return Result::Err(DecodeError::InvalidCharacter(pos));
}

/// Decode a URL-safe Base64 string to bytes (handles missing padding).
function base64url_decode(input: ref<str>): Result<own<Vec<u8>>, DecodeError> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  if len == 0 { return Result::Ok(Vec::new()); }
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
