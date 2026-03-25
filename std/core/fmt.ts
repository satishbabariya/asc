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
    // DECISION: Integer-to-string conversion done by runtime.
    return f.write_str("i32");
  }
}

impl Display for i64 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str("i64");
  }
}

impl Display for f64 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str("f64");
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

impl Debug for i32 {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str("i32");
  }
}

impl Debug for bool {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    if *self { return f.write_str("true"); }
    return f.write_str("false");
  }
}
