// std/encoding/utf16.ts — UTF-8 ↔ UTF-16 conversion (RFC-0018)

/// Error type for UTF-16 encoding/decoding.
enum Utf16Error {
  InvalidUtf8(usize),
  InvalidUtf16(usize),
  UnpairedSurrogate(usize),
}

impl Display for Utf16Error {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      Utf16Error::InvalidUtf8(pos) => f.write_str("invalid UTF-8 at ").and_then(|_| pos.fmt(f)),
      Utf16Error::InvalidUtf16(pos) => f.write_str("invalid UTF-16 at ").and_then(|_| pos.fmt(f)),
      Utf16Error::UnpairedSurrogate(pos) => f.write_str("unpaired surrogate at ").and_then(|_| pos.fmt(f)),
    }
  }
}

/// Encode a UTF-8 string to UTF-16 (little-endian) bytes.
function encode(input: ref<str>): Result<own<Vec<u16>>, Utf16Error> {
  let bytes = input.as_bytes();
  let len = bytes.len();
  let result: own<Vec<u16>> = Vec::new();
  let i: usize = 0;

  while i < len {
    let b0 = bytes[i];
    let cp: u32 = 0;
    let seq_len: usize = 0;

    if b0 < 0x80 {
      cp = b0 as u32;
      seq_len = 1;
    } else if (b0 & 0xE0) == 0xC0 {
      if i + 1 >= len { return Result::Err(Utf16Error::InvalidUtf8(i)); }
      cp = ((b0 & 0x1F) as u32) << 6 | ((bytes[i + 1] & 0x3F) as u32);
      seq_len = 2;
    } else if (b0 & 0xF0) == 0xE0 {
      if i + 2 >= len { return Result::Err(Utf16Error::InvalidUtf8(i)); }
      cp = ((b0 & 0x0F) as u32) << 12
         | ((bytes[i + 1] & 0x3F) as u32) << 6
         | ((bytes[i + 2] & 0x3F) as u32);
      seq_len = 3;
    } else if (b0 & 0xF8) == 0xF0 {
      if i + 3 >= len { return Result::Err(Utf16Error::InvalidUtf8(i)); }
      cp = ((b0 & 0x07) as u32) << 18
         | ((bytes[i + 1] & 0x3F) as u32) << 12
         | ((bytes[i + 2] & 0x3F) as u32) << 6
         | ((bytes[i + 3] & 0x3F) as u32);
      seq_len = 4;
    } else {
      return Result::Err(Utf16Error::InvalidUtf8(i));
    }

    if cp < 0x10000 {
      result.push(cp as u16);
    } else {
      // Surrogate pair.
      let adjusted = cp - 0x10000;
      result.push((0xD800 + (adjusted >> 10)) as u16);
      result.push((0xDC00 + (adjusted & 0x3FF)) as u16);
    }

    i = i + seq_len;
  }

  return Result::Ok(result);
}

/// Decode UTF-16 code units to a UTF-8 string.
function decode(input: ref<[u16]>): Result<own<String>, Utf16Error> {
  let len = input.len();
  let result = String::new();
  let i: usize = 0;

  while i < len {
    let unit = input[i];
    let cp: u32 = 0;

    if unit >= 0xD800 && unit <= 0xDBFF {
      // High surrogate — expect low surrogate.
      if i + 1 >= len {
        return Result::Err(Utf16Error::UnpairedSurrogate(i));
      }
      let low = input[i + 1];
      if low < 0xDC00 || low > 0xDFFF {
        return Result::Err(Utf16Error::UnpairedSurrogate(i));
      }
      cp = 0x10000 + (((unit - 0xD800) as u32) << 10) + ((low - 0xDC00) as u32);
      i = i + 2;
    } else if unit >= 0xDC00 && unit <= 0xDFFF {
      return Result::Err(Utf16Error::UnpairedSurrogate(i));
    } else {
      cp = unit as u32;
      i = i + 1;
    }

    // Encode code point as UTF-8.
    if cp < 0x80 {
      result.push(cp as char);
    } else if cp < 0x800 {
      result.push((0xC0 | (cp >> 6)) as char);
      result.push((0x80 | (cp & 0x3F)) as char);
    } else if cp < 0x10000 {
      result.push((0xE0 | (cp >> 12)) as char);
      result.push((0x80 | ((cp >> 6) & 0x3F)) as char);
      result.push((0x80 | (cp & 0x3F)) as char);
    } else {
      result.push((0xF0 | (cp >> 18)) as char);
      result.push((0x80 | ((cp >> 12) & 0x3F)) as char);
      result.push((0x80 | ((cp >> 6) & 0x3F)) as char);
      result.push((0x80 | (cp & 0x3F)) as char);
    }
  }

  return Result::Ok(result);
}
