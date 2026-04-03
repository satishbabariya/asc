// std/encoding/hex.ts — Hex encode/decode (RFC-0018)

import { DecodeError } from './base64';

const HEX_LOWER: [u8; 16] = [
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
];

/// Encode bytes as lowercase hexadecimal string.
function encode(input: ref<[u8]>): own<String> {
  let result = String::with_capacity(input.len() * 2);
  let i: usize = 0;
  while i < input.len() {
    let b = input[i];
    result.push(HEX_LOWER[(b >> 4) as usize] as char);
    result.push(HEX_LOWER[(b & 0x0F) as usize] as char);
    i = i + 1;
  }
  return result;
}

/// Decode a hexadecimal string to bytes.
function decode(input: ref<str>): Result<own<Vec<u8>>, DecodeError> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  if len % 2 != 0 {
    return Result::Err(DecodeError::InvalidLength);
  }
  let result: own<Vec<u8>> = Vec::with_capacity(len / 2);
  let i: usize = 0;
  while i < len {
    let hi = hex_val(bytes[i], i)?;
    let lo = hex_val(bytes[i + 1], i + 1)?;
    result.push((hi << 4) | lo);
    i = i + 2;
  }
  return Result::Ok(result);
}

/// Convert a single hex character to its 4-bit value.
function hex_val(c: u8, pos: usize): Result<u8, DecodeError> {
  if c >= 0x30 && c <= 0x39 { return Result::Ok(c - 0x30); }       // 0-9
  if c >= 0x41 && c <= 0x46 { return Result::Ok(c - 0x41 + 10); }  // A-F
  if c >= 0x61 && c <= 0x66 { return Result::Ok(c - 0x61 + 10); }  // a-f
  return Result::Err(DecodeError::InvalidCharacter(pos));
}
