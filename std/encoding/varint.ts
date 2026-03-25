// std/encoding/varint.ts — LEB128 variable-length integer encoding (RFC-0018)

/// Error for varint decode failures.
enum VarintError {
  Overflow,
  UnexpectedEof,
}

impl Display for VarintError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      VarintError::Overflow => f.write_str("varint overflow"),
      VarintError::UnexpectedEof => f.write_str("unexpected end of varint"),
    }
  }
}

/// Encode a u32 as unsigned LEB128, appending bytes to buf.
function encode_u32(buf: refmut<Vec<u8>>, value: u32): void {
  let v = value;
  loop {
    let byte = (v & 0x7F) as u8;
    v = v >> 7;
    if v != 0 {
      byte = byte | 0x80;
    }
    buf.push(byte);
    if v == 0 { break; }
  }
}

/// Decode an unsigned LEB128 u32 from a byte slice.
/// Returns (decoded_value, bytes_consumed).
function decode_u32(input: ref<[u8]>): Result<(u32, usize), VarintError> {
  let result: u32 = 0;
  let shift: u32 = 0;
  let i: usize = 0;

  while i < input.len() {
    let byte = input[i];
    if shift >= 35 {
      return Result::Err(VarintError::Overflow);
    }
    result = result | (((byte & 0x7F) as u32) << shift);
    i = i + 1;
    if (byte & 0x80) == 0 {
      return Result::Ok((result, i));
    }
    shift = shift + 7;
  }

  return Result::Err(VarintError::UnexpectedEof);
}

/// Encode a u64 as unsigned LEB128, appending bytes to buf.
function encode_u64(buf: refmut<Vec<u8>>, value: u64): void {
  let v = value;
  loop {
    let byte = (v & 0x7F) as u8;
    v = v >> 7;
    if v != 0 {
      byte = byte | 0x80;
    }
    buf.push(byte);
    if v == 0 { break; }
  }
}

/// Decode an unsigned LEB128 u64 from a byte slice.
/// Returns (decoded_value, bytes_consumed).
function decode_u64(input: ref<[u8]>): Result<(u64, usize), VarintError> {
  let result: u64 = 0;
  let shift: u32 = 0;
  let i: usize = 0;

  while i < input.len() {
    let byte = input[i];
    if shift >= 70 {
      return Result::Err(VarintError::Overflow);
    }
    result = result | (((byte & 0x7F) as u64) << shift);
    i = i + 1;
    if (byte & 0x80) == 0 {
      return Result::Ok((result, i));
    }
    shift = shift + 7;
  }

  return Result::Err(VarintError::UnexpectedEof);
}

/// Encode a signed i32 as signed LEB128.
function encode_i32(buf: refmut<Vec<u8>>, value: i32): void {
  let v = value;
  let more = true;
  while more {
    let byte = (v & 0x7F) as u8;
    v = v >> 7;
    // If the sign bit of byte is set and v is -1, or sign bit clear and v is 0, we're done.
    if (v == 0 && (byte & 0x40) == 0) || (v == -1 && (byte & 0x40) != 0) {
      more = false;
    } else {
      byte = byte | 0x80;
    }
    buf.push(byte);
  }
}

/// Decode a signed LEB128 i32 from a byte slice.
/// Returns (decoded_value, bytes_consumed).
function decode_i32(input: ref<[u8]>): Result<(i32, usize), VarintError> {
  let result: i32 = 0;
  let shift: u32 = 0;
  let i: usize = 0;
  let byte: u8 = 0;

  while i < input.len() {
    byte = input[i];
    if shift >= 35 {
      return Result::Err(VarintError::Overflow);
    }
    result = result | (((byte & 0x7F) as i32) << shift);
    shift = shift + 7;
    i = i + 1;
    if (byte & 0x80) == 0 { break; }
  }

  if i == 0 { return Result::Err(VarintError::UnexpectedEof); }
  if (byte & 0x80) != 0 { return Result::Err(VarintError::UnexpectedEof); }

  // Sign-extend if needed.
  if shift < 32 && (byte & 0x40) != 0 {
    result = result | ((-1 as i32) << shift);
  }

  return Result::Ok((result, i));
}
