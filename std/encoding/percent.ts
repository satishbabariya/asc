// std/encoding/percent.ts — RFC 3986 percent-encoding (RFC-0018)

/// Unreserved characters per RFC 3986: A-Z a-z 0-9 - . _ ~
/// Returns true if the byte does NOT need percent-encoding.
function is_unreserved(c: u8): bool {
  // A-Z
  if c >= 0x41 && c <= 0x5A { return true; }
  // a-z
  if c >= 0x61 && c <= 0x7A { return true; }
  // 0-9
  if c >= 0x30 && c <= 0x39 { return true; }
  // - . _ ~
  if c == 0x2D || c == 0x2E || c == 0x5F || c == 0x7E { return true; }
  return false;
}

const HEX_UPPER: [u8; 16] = [
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
];

/// Encode a string using RFC 3986 percent-encoding.
/// Unreserved characters (A-Z a-z 0-9 - . _ ~) are passed through as-is.
/// All other bytes are encoded as %XX (uppercase hex).
function encode(input: ref<str>): own<String> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  let result = String::with_capacity(len * 3);
  let i: usize = 0;
  while i < len {
    let b = bytes[i];
    if is_unreserved(b) {
      result.push(b as char);
    } else {
      result.push(0x25 as char); // '%'
      result.push(HEX_UPPER[(b >> 4) as usize] as char);
      result.push(HEX_UPPER[(b & 0x0F) as usize] as char);
    }
    i = i + 1;
  }
  return result;
}

/// Decode a percent-encoded string back to its original form.
/// Invalid %XX sequences are passed through as-is.
function decode(input: ref<str>): own<String> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  let result = String::with_capacity(len);
  let i: usize = 0;
  while i < len {
    let b = bytes[i];
    if b == 0x25 && i + 2 < len {
      // '%' — try to decode the next two hex digits.
      let hi = hex_digit(bytes[i + 1]);
      let lo = hex_digit(bytes[i + 2]);
      if hi >= 0 && lo >= 0 {
        let decoded = ((hi as u8) << 4) | (lo as u8);
        result.push(decoded as char);
        i = i + 3;
      } else {
        // Invalid hex digits — emit '%' literally.
        result.push(b as char);
        i = i + 1;
      }
    } else {
      result.push(b as char);
      i = i + 1;
    }
  }
  return result;
}

/// Encode a URI component. Same as encode — all bytes except unreserved
/// characters (A-Z a-z 0-9 - . _ ~) are percent-encoded.
/// This matches the behavior of JavaScript's encodeURIComponent for
/// the unreserved set.
function encode_component(input: ref<str>): own<String> {
  return encode(input);
}

/// Convert a single hex ASCII character to its 4-bit numeric value.
/// Returns -1 if the character is not a valid hex digit.
function hex_digit(c: u8): i32 {
  if c >= 0x30 && c <= 0x39 { return (c - 0x30) as i32; }  // 0-9
  if c >= 0x41 && c <= 0x46 { return (c - 0x41 + 10) as i32; }  // A-F
  if c >= 0x61 && c <= 0x66 { return (c - 0x61 + 10) as i32; }  // a-f
  return -1;
}
