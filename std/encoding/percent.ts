// std/encoding/percent.ts — RFC 3986 percent-encoding (RFC-0018 §1.5)

/// Error for strict percent-decoding.
/// `InvalidSequence(pos)` — a '%' at `pos` was not followed by two hex digits,
/// or the decoded byte stream is not valid UTF-8.
enum PercentError {
  InvalidSequence(usize),
}

impl Display for PercentError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      PercentError::InvalidSequence(pos) =>
        f.write_str("invalid percent-encoded sequence at ").and_then(|_| pos.fmt(f)),
    }
  }
}

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

/// U+FFFD replacement character, UTF-8 encoded: EF BF BD.
const REPLACEMENT_UTF8: [u8; 3] = [0xEF, 0xBF, 0xBD];

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

/// Encode with percent-encoding, preserving chars in `allowed` unencoded.
/// Unreserved characters are always preserved; `allowed` extends that set.
function encode_except(input: ref<str>, allowed: ref<str>): own<String> {
  let result = String::new();
  let bytes = input.as_bytes();
  let allowed_bytes = allowed.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    let c = bytes[i];
    if is_unreserved(c) {
      result.push(c as char);
    } else {
      let is_safe = false;
      let j: usize = 0;
      while j < allowed_bytes.len() {
        if c == allowed_bytes[j] { is_safe = true; break; }
        j = j + 1;
      }
      if is_safe {
        result.push(c as char);
      } else {
        result.push(0x25 as char); // '%'
        result.push(HEX_UPPER[(c >> 4) as usize] as char);
        result.push(HEX_UPPER[(c & 0x0F) as usize] as char);
      }
    }
    i = i + 1;
  }
  return result;
}

/// Encode a URI path segment. In addition to the unreserved set, the
/// sub-delims and `:` `@` are permitted in `pchar` per RFC 3986 §3.3,
/// but `/` MUST be encoded because it is the segment separator.
function encode_path_segment(input: ref<str>): own<String> {
  // sub-delims (RFC 3986 §2.2) plus ':' and '@', minus '/'.
  // sub-delims = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / "," / ";" / "="
  return encode_except(input, "!$&'()*+,;=:@");
}

/// Encode a URI query component. `&` and `=` are key/value separators and
/// MUST be encoded when they appear inside a component value. Most other
/// sub-delims are safe; so are `:` `@` `/` `?` per the query production.
function encode_query_component(input: ref<str>): own<String> {
  return encode_except(input, "!$'()*+,;:@/?");
}

/// Encode a URI component. Alias for `encode` — all bytes except unreserved
/// characters (A-Z a-z 0-9 - . _ ~) are percent-encoded. This matches the
/// behavior of JavaScript's encodeURIComponent for the unreserved set.
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

/// Decode a percent-encoded string into raw bytes, strictly validating
/// every `%XX` sequence. On any malformed escape, returns the byte offset
/// of the offending `%`.
function decode_bytes(input: ref<str>): Result<own<Vec<u8>>, PercentError> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  let out: own<Vec<u8>> = Vec::with_capacity(len);
  let i: usize = 0;
  while i < len {
    let b = bytes[i];
    if b == 0x25 { // '%'
      if i + 2 >= len {
        return Result::Err(PercentError::InvalidSequence(i));
      }
      let hi = hex_digit(bytes[i + 1]);
      let lo = hex_digit(bytes[i + 2]);
      if hi < 0 || lo < 0 {
        return Result::Err(PercentError::InvalidSequence(i));
      }
      out.push(((hi as u8) << 4) | (lo as u8));
      i = i + 3;
    } else {
      // '+' is NOT decoded to space here — that is form-urlencoded
      // behavior (application/x-www-form-urlencoded), not RFC 3986.
      out.push(b);
      i = i + 1;
    }
  }
  return Result::Ok(out);
}

/// Return the length of the well-formed UTF-8 sequence starting at `pos`
/// in `bytes`, or 0 if the sequence is invalid. Covers overlong encodings,
/// lone surrogates (ED A0..BF), and out-of-range code points (> U+10FFFF).
function utf8_next(bytes: ref<Vec<u8>>, pos: usize): usize {
  let len = bytes.len();
  if pos >= len { return 0; }
  let b = bytes[pos];
  if b < 0x80 {
    return 1;
  }
  if (b & 0xE0) == 0xC0 {
    if b < 0xC2 { return 0; }
    if pos + 1 >= len { return 0; }
    if (bytes[pos + 1] & 0xC0) != 0x80 { return 0; }
    return 2;
  }
  if (b & 0xF0) == 0xE0 {
    if pos + 2 >= len { return 0; }
    let b1 = bytes[pos + 1];
    let b2 = bytes[pos + 2];
    if (b1 & 0xC0) != 0x80 { return 0; }
    if (b2 & 0xC0) != 0x80 { return 0; }
    if b == 0xE0 && b1 < 0xA0 { return 0; }
    if b == 0xED && b1 > 0x9F { return 0; }
    return 3;
  }
  if (b & 0xF8) == 0xF0 {
    if b > 0xF4 { return 0; }
    if pos + 3 >= len { return 0; }
    let b1 = bytes[pos + 1];
    let b2 = bytes[pos + 2];
    let b3 = bytes[pos + 3];
    if (b1 & 0xC0) != 0x80 { return 0; }
    if (b2 & 0xC0) != 0x80 { return 0; }
    if (b3 & 0xC0) != 0x80 { return 0; }
    if b == 0xF0 && b1 < 0x90 { return 0; }
    if b == 0xF4 && b1 > 0x8F { return 0; }
    return 4;
  }
  return 0;
}

/// Validate that a byte slice is well-formed UTF-8. Returns the index of
/// the first invalid byte on failure, or the input length on success.
function utf8_validate(bytes: ref<Vec<u8>>): usize {
  let len = bytes.len();
  let i: usize = 0;
  while i < len {
    let seq = utf8_next(bytes, i);
    if seq == 0 { return i; }
    i = i + seq;
  }
  return len;
}

/// Decode a percent-encoded string back to a `String`, strictly validating
/// both the `%XX` escapes and the resulting UTF-8. On any malformed escape
/// or invalid UTF-8, returns `PercentError::InvalidSequence(pos)`.
function decode(input: ref<str>): Result<own<String>, PercentError> {
  let bytes_result = decode_bytes(input);
  let bytes = match bytes_result {
    Result::Ok(v) => v,
    Result::Err(e) => return Result::Err(e),
  };
  let bad = utf8_validate(&bytes);
  if bad != bytes.len() {
    return Result::Err(PercentError::InvalidSequence(bad));
  }
  let s = String::with_capacity(bytes.len());
  let i: usize = 0;
  while i < bytes.len() {
    s.push(bytes[i] as char);
    i = i + 1;
  }
  return Result::Ok(s);
}

/// Decode a percent-encoded string, replacing invalid UTF-8 sequences
/// with the Unicode replacement character U+FFFD. Malformed `%XX`
/// escapes are passed through literally (the `%` remains, and the
/// following characters are processed normally).
function decode_lossy(input: ref<str>): own<String> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  let buf: own<Vec<u8>> = Vec::with_capacity(len);
  let i: usize = 0;
  while i < len {
    let b = bytes[i];
    if b == 0x25 && i + 2 < len {
      let hi = hex_digit(bytes[i + 1]);
      let lo = hex_digit(bytes[i + 2]);
      if hi >= 0 && lo >= 0 {
        buf.push(((hi as u8) << 4) | (lo as u8));
        i = i + 3;
      } else {
        // Invalid escape — keep '%' literal, resync to next byte.
        buf.push(b);
        i = i + 1;
      }
    } else {
      buf.push(b);
      i = i + 1;
    }
  }
  // Walk `buf` as UTF-8, emitting U+FFFD for any ill-formed subsequence.
  let out = String::with_capacity(buf.len());
  let j: usize = 0;
  let n = buf.len();
  while j < n {
    let seq = utf8_next(&buf, j);
    if seq == 0 {
      // Invalid — emit U+FFFD once per ill-formed byte and advance by 1.
      out.push(REPLACEMENT_UTF8[0] as char);
      out.push(REPLACEMENT_UTF8[1] as char);
      out.push(REPLACEMENT_UTF8[2] as char);
      j = j + 1;
    } else {
      let k: usize = 0;
      while k < seq {
        out.push(buf[j + k] as char);
        k = k + 1;
      }
      j = j + seq;
    }
  }
  return out;
}

/// Decode a percent-encoded byte buffer in place, collapsing `%XX`
/// escapes to their byte values. Unlike `decode`, this does not require
/// the result to be valid UTF-8 — callers working with raw bytes (form
/// data, opaque blobs) can decode without a round-trip through `String`.
/// On malformed `%XX` the buffer is left at its original contents up to
/// the offending position and `PercentError::InvalidSequence` is returned.
function decode_in_place(buf: refmut<Vec<u8>>): Result<void, PercentError> {
  let len = buf.len();
  let read: usize = 0;
  let write: usize = 0;
  while read < len {
    let b = buf[read];
    if b == 0x25 { // '%'
      if read + 2 >= len {
        return Result::Err(PercentError::InvalidSequence(read));
      }
      let hi = hex_digit(buf[read + 1]);
      let lo = hex_digit(buf[read + 2]);
      if hi < 0 || lo < 0 {
        return Result::Err(PercentError::InvalidSequence(read));
      }
      buf[write] = ((hi as u8) << 4) | (lo as u8);
      read = read + 3;
      write = write + 1;
    } else {
      buf[write] = b;
      read = read + 1;
      write = write + 1;
    }
  }
  buf.truncate(write);
  return Result::Ok(());
}
