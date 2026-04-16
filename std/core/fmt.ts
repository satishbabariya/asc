// std/core/fmt.ts — Display, Debug traits and Formatter (RFC-0011/RFC-0013)

/// Display trait for user-facing output (used by {} in format strings).
trait Display {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError>;
}

/// Debug trait for developer-facing output (used by {:?} in format strings).
trait Debug {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError>;
}

/// LowerHex: {:x} formatting.
trait LowerHex {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError>;
}

/// UpperHex: {:X} formatting.
trait UpperHex {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError>;
}

/// Binary: {:b} formatting.
trait Binary {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError>;
}

/// Octal: {:o} formatting.
trait Octal {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError>;
}

/// Formatting error type.
struct FmtError;

/// Formatter wraps a write sink and format options.
struct Formatter {
  buf: own<String>,
  fill: char,
  width: Option<usize>,
  precision: Option<usize>,
  alternate: bool,
}

impl Formatter {
  fn new(): own<Formatter> {
    return Formatter {
      buf: String::new(),
      fill: ' ',
      width: Option::None,
      precision: Option::None,
      alternate: false,
    };
  }

  fn write_str(refmut<Self>, s: ref<str>): Result<void, FmtError> {
    self.buf.push_str(s);
    return Result::Ok(());
  }

  fn write_char(refmut<Self>, c: char): Result<void, FmtError> {
    self.buf.push(c);
    return Result::Ok(());
  }

  fn into_string(own<Self>): own<String> {
    return self.buf;
  }
}

// Built-in Display implementations for primitives.
impl Display for i32 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    let n = *self;
    if n == 0 { return f.write_str("0"); }
    let buf: [u8; 11] = [0; 11];
    let pos: usize = 11;
    let neg = false;
    if n < 0 { neg = true; n = 0 - n; }
    while n > 0 {
      pos = pos - 1;
      buf[pos] = (48 + (n % 10)) as u8;
      n = n / 10;
    }
    if neg {
      pos = pos - 1;
      buf[pos] = 45;
    }
    const s = unsafe { str::from_raw_parts(&buf[pos] as *const u8, 11 - pos) };
    return f.write_str(s);
  }
}

impl Display for i64 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    let n = *self;
    if n == 0 { return f.write_str("0"); }
    let buf: [u8; 20] = [0; 20];
    let pos: usize = 20;
    let neg = false;
    if n < 0 { neg = true; n = 0 - n; }
    while n > 0 {
      pos = pos - 1;
      buf[pos] = (48 + (n % 10) as u8);
      n = n / 10;
    }
    if neg {
      pos = pos - 1;
      buf[pos] = 45;
    }
    const s = unsafe { str::from_raw_parts(&buf[pos] as *const u8, 20 - pos) };
    return f.write_str(s);
  }
}

impl Display for f64 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    let val = *self;
    if val < 0.0 {
      f.write_char('-');
      val = 0.0 - val;
    }
    let int_part = val as i64;
    let frac = val - (int_part as f64);
    let ibuf: [u8; 20] = [0; 20];
    let ipos: usize = 20;
    if int_part == 0 {
      ipos = ipos - 1;
      ibuf[ipos] = 48;
    } else {
      while int_part > 0 {
        ipos = ipos - 1;
        ibuf[ipos] = (48 + (int_part % 10) as u8);
        int_part = int_part / 10;
      }
    }
    const is = unsafe { str::from_raw_parts(&ibuf[ipos] as *const u8, 20 - ipos) };
    f.write_str(is);
    f.write_char('.');
    let digits: usize = 6;
    let fbuf: [u8; 6] = [0; 6];
    let d: usize = 0;
    while d < digits {
      frac = frac * 10.0;
      const digit = frac as i32;
      fbuf[d] = (48 + digit) as u8;
      frac = frac - (digit as f64);
      d = d + 1;
    }
    const fs = unsafe { str::from_raw_parts(&fbuf[0] as *const u8, digits) };
    return f.write_str(fs);
  }
}

impl Display for bool {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    if *self { return f.write_str("true"); }
    return f.write_str("false");
  }
}

impl Display for str {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str(self);
  }
}

impl Display for u32 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    // Convert u32 to decimal string.
    if *self == 0 { return f.write_str("0"); }
    let buf: [u8; 10] = [0; 10];
    let pos: i32 = 9;
    let n = *self;
    while n > 0 {
      buf[pos as usize] = (48 + (n % 10)) as u8;
      n = n / 10;
      pos = pos - 1;
    }
    pos = pos + 1;
    const start = pos as usize;
    const len = 10 - start;
    return f.write_str(unsafe { str::from_raw_parts(&buf[start] as *const u8, len) });
  }
}

impl Display for u64 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    if *self == 0 { return f.write_str("0"); }
    let buf: [u8; 20] = [0; 20];
    let pos: i32 = 19;
    let n = *self;
    while n > 0 {
      buf[pos as usize] = (48 + (n % 10) as u32) as u8;
      n = n / 10;
      pos = pos - 1;
    }
    pos = pos + 1;
    const start = pos as usize;
    const len = 20 - start;
    return f.write_str(unsafe { str::from_raw_parts(&buf[start] as *const u8, len) });
  }
}

impl Display for f32 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    // Simplified: convert to f64 and delegate.
    const as_f64 = *self as f64;
    return as_f64.fmt(f);
  }
}

impl Display for char {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    // Write the char as a single-character string.
    let buf: [u8; 4] = [0; 4];
    const cp = *self as u32;
    if cp < 0x80 {
      buf[0] = cp as u8;
      return f.write_str(unsafe { str::from_raw_parts(&buf[0] as *const u8, 1) });
    } else if cp < 0x800 {
      buf[0] = (0xC0 | (cp >> 6)) as u8;
      buf[1] = (0x80 | (cp & 0x3F)) as u8;
      return f.write_str(unsafe { str::from_raw_parts(&buf[0] as *const u8, 2) });
    } else if cp < 0x10000 {
      buf[0] = (0xE0 | (cp >> 12)) as u8;
      buf[1] = (0x80 | ((cp >> 6) & 0x3F)) as u8;
      buf[2] = (0x80 | (cp & 0x3F)) as u8;
      return f.write_str(unsafe { str::from_raw_parts(&buf[0] as *const u8, 3) });
    } else {
      buf[0] = (0xF0 | (cp >> 18)) as u8;
      buf[1] = (0x80 | ((cp >> 12) & 0x3F)) as u8;
      buf[2] = (0x80 | ((cp >> 6) & 0x3F)) as u8;
      buf[3] = (0x80 | (cp & 0x3F)) as u8;
      return f.write_str(unsafe { str::from_raw_parts(&buf[0] as *const u8, 4) });
    }
  }
}

impl Debug for i32 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return Display::fmt(self, f);
  }
}

impl Debug for bool {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    if *self { return f.write_str("true"); }
    return f.write_str("false");
  }
}
