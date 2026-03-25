// std/config/toml.ts — TOML parser/serializer (RFC-0019)

/// Represents a TOML value.
enum TomlValue {
  String(own<String>),
  Integer(i64),
  Float(f64),
  Boolean(bool),
  DateTime(own<String>),
  Array(own<Vec<TomlValue>>),
  Table(own<Vec<(own<String>, TomlValue)>>),
}

/// Error type for TOML parsing.
enum TomlError {
  UnexpectedToken(usize, own<String>),
  UnexpectedEof,
  DuplicateKey(own<String>),
  InvalidValue(usize),
}

impl Display for TomlError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      TomlError::UnexpectedToken(line, msg) => {
        f.write_str("line ")?; line.fmt(f)?; f.write_str(": ")?; f.write_str(msg.as_str())
      },
      TomlError::UnexpectedEof => f.write_str("unexpected end of TOML input"),
      TomlError::DuplicateKey(key) => f.write_str("duplicate key: ").and_then(|_| f.write_str(key.as_str())),
      TomlError::InvalidValue(line) => f.write_str("invalid value at line ").and_then(|_| line.fmt(f)),
    }
  }
}

impl TomlValue {
  fn as_str(ref<Self>): Option<ref<str>> {
    match self { TomlValue::String(s) => Option::Some(s.as_str()), _ => Option::None }
  }
  fn as_i64(ref<Self>): Option<i64> {
    match self { TomlValue::Integer(v) => Option::Some(*v), _ => Option::None }
  }
  fn as_f64(ref<Self>): Option<f64> {
    match self { TomlValue::Float(v) => Option::Some(*v), _ => Option::None }
  }
  fn as_bool(ref<Self>): Option<bool> {
    match self { TomlValue::Boolean(v) => Option::Some(*v), _ => Option::None }
  }
  fn as_array(ref<Self>): Option<ref<Vec<TomlValue>>> {
    match self { TomlValue::Array(a) => Option::Some(a), _ => Option::None }
  }
  fn as_table(ref<Self>): Option<ref<Vec<(own<String>, TomlValue)>>> {
    match self { TomlValue::Table(t) => Option::Some(t), _ => Option::None }
  }

  /// Lookup a key in a TOML table.
  fn get(ref<Self>, key: ref<str>): Option<ref<TomlValue>> {
    match self {
      TomlValue::Table(entries) => {
        let i: usize = 0;
        while i < entries.len() {
          let entry = entries.get(i).unwrap();
          if entry.0.as_str() == key { return Option::Some(&entry.1); }
          i = i + 1;
        }
        return Option::None;
      },
      _ => Option::None,
    }
  }
}

/// Internal parser state.
struct TomlParser {
  input: ref<str>,
  pos: usize,
  line: usize,
}

/// Parse a TOML string into a TomlValue::Table.
function parse(input: ref<str>): Result<own<TomlValue>, TomlError> {
  let parser = TomlParser { input: input, pos: 0, line: 1 };
  let root: own<Vec<(own<String>, TomlValue)>> = Vec::new();
  let current_table = refmut root;

  while parser.pos < parser.input.len() {
    parser.skip_whitespace_and_newlines();
    if parser.pos >= parser.input.len() { break; }

    let c = parser.peek();
    if c == 0x23 { // '#' comment
      parser.skip_line();
      continue;
    }
    if c == 0x5B { // '[' table header
      parser.advance();
      let is_array = false;
      if parser.pos < parser.input.len() && parser.peek() == 0x5B {
        is_array = true;
        parser.advance();
      }
      parser.skip_ws();
      let key = parser.parse_key()?;
      parser.skip_ws();
      if is_array {
        parser.expect(0x5D)?;
        parser.expect(0x5D)?;
      } else {
        parser.expect(0x5D)?;
      }
      // Create nested table in root.
      let new_table: own<Vec<(own<String>, TomlValue)>> = Vec::new();
      root.push((key, TomlValue::Table(new_table)));
      let last_idx = root.len() - 1;
      // Point current_table at the newly created table's entries.
      match root.get_mut(last_idx).unwrap().1 {
        TomlValue::Table(t) => { current_table = t; },
        _ => {},
      }
      continue;
    }

    // Key = Value pair.
    let key = parser.parse_key()?;
    parser.skip_ws();
    parser.expect(0x3D)?; // '='
    parser.skip_ws();
    let value = parser.parse_value()?;
    current_table.push((key, value));
    parser.skip_ws();
    parser.skip_comment();
    parser.skip_newline();
  }

  return Result::Ok(TomlValue::Table(root));
}

impl TomlParser {
  fn peek(ref<Self>): u8 { return self.input.as_bytes()[self.pos]; }
  fn advance(refmut<Self>): u8 {
    let c = self.input.as_bytes()[self.pos];
    self.pos = self.pos + 1;
    if c == 0x0A { self.line = self.line + 1; }
    return c;
  }
  fn expect(refmut<Self>, c: u8): Result<void, TomlError> {
    if self.pos >= self.input.len() { return Result::Err(TomlError::UnexpectedEof); }
    if self.peek() != c {
      return Result::Err(TomlError::UnexpectedToken(self.line, String::from("unexpected character")));
    }
    self.advance();
    return Result::Ok(void);
  }
  fn skip_ws(refmut<Self>): void {
    while self.pos < self.input.len() {
      let c = self.peek();
      if c == 0x20 || c == 0x09 { self.advance(); }
      else { break; }
    }
  }
  fn skip_whitespace_and_newlines(refmut<Self>): void {
    while self.pos < self.input.len() {
      let c = self.peek();
      if c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D { self.advance(); }
      else { break; }
    }
  }
  fn skip_line(refmut<Self>): void {
    while self.pos < self.input.len() && self.peek() != 0x0A { self.advance(); }
    if self.pos < self.input.len() { self.advance(); }
  }
  fn skip_comment(refmut<Self>): void {
    if self.pos < self.input.len() && self.peek() == 0x23 { self.skip_line(); }
  }
  fn skip_newline(refmut<Self>): void {
    if self.pos < self.input.len() && self.peek() == 0x0D { self.advance(); }
    if self.pos < self.input.len() && self.peek() == 0x0A { self.advance(); }
  }

  fn parse_key(refmut<Self>): Result<own<String>, TomlError> {
    if self.pos >= self.input.len() { return Result::Err(TomlError::UnexpectedEof); }
    let c = self.peek();
    if c == 0x22 { return self.parse_basic_string(); }
    if c == 0x27 { return self.parse_literal_string(); }
    // Bare key: [A-Za-z0-9_-]
    let start = self.pos;
    while self.pos < self.input.len() {
      let ch = self.peek();
      if (ch >= 0x41 && ch <= 0x5A) || (ch >= 0x61 && ch <= 0x7A)
        || (ch >= 0x30 && ch <= 0x39) || ch == 0x5F || ch == 0x2D {
        self.advance();
      } else { break; }
    }
    if self.pos == start {
      return Result::Err(TomlError::UnexpectedToken(self.line, String::from("expected key")));
    }
    return Result::Ok(String::from(self.input.slice(start, self.pos)));
  }

  fn parse_value(refmut<Self>): Result<own<TomlValue>, TomlError> {
    if self.pos >= self.input.len() { return Result::Err(TomlError::UnexpectedEof); }
    let c = self.peek();
    if c == 0x22 { // string
      let s = self.parse_basic_string()?;
      return Result::Ok(TomlValue::String(s));
    }
    if c == 0x27 { // literal string
      let s = self.parse_literal_string()?;
      return Result::Ok(TomlValue::String(s));
    }
    if c == 0x74 { // true
      self.expect(0x74)?; self.expect(0x72)?; self.expect(0x75)?; self.expect(0x65)?;
      return Result::Ok(TomlValue::Boolean(true));
    }
    if c == 0x66 { // false
      self.expect(0x66)?; self.expect(0x61)?; self.expect(0x6C)?; self.expect(0x73)?; self.expect(0x65)?;
      return Result::Ok(TomlValue::Boolean(false));
    }
    if c == 0x5B { // array
      return self.parse_array();
    }
    if c == 0x7B { // inline table
      return self.parse_inline_table();
    }
    // Number (integer or float).
    return self.parse_number();
  }

  fn parse_basic_string(refmut<Self>): Result<own<String>, TomlError> {
    self.expect(0x22)?;
    let result = String::new();
    while self.pos < self.input.len() {
      let c = self.advance();
      if c == 0x22 { return Result::Ok(result); }
      if c == 0x5C { // escape
        if self.pos >= self.input.len() { return Result::Err(TomlError::UnexpectedEof); }
        let esc = self.advance();
        if esc == 0x6E { result.push(0x0A as char); }
        else if esc == 0x74 { result.push(0x09 as char); }
        else if esc == 0x72 { result.push(0x0D as char); }
        else if esc == 0x5C { result.push('\\' as char); }
        else if esc == 0x22 { result.push('"' as char); }
        else { result.push(esc as char); }
      } else {
        result.push(c as char);
      }
    }
    return Result::Err(TomlError::UnexpectedEof);
  }

  fn parse_literal_string(refmut<Self>): Result<own<String>, TomlError> {
    self.expect(0x27)?;
    let start = self.pos;
    while self.pos < self.input.len() {
      if self.peek() == 0x27 {
        let s = String::from(self.input.slice(start, self.pos));
        self.advance();
        return Result::Ok(s);
      }
      self.advance();
    }
    return Result::Err(TomlError::UnexpectedEof);
  }

  fn parse_number(refmut<Self>): Result<own<TomlValue>, TomlError> {
    let start = self.pos;
    let is_float = false;
    if self.peek() == 0x2D || self.peek() == 0x2B { self.advance(); }
    while self.pos < self.input.len() {
      let c = self.peek();
      if c >= 0x30 && c <= 0x39 { self.advance(); }
      else if c == 0x5F { self.advance(); } // underscore separator
      else if c == 0x2E || c == 0x65 || c == 0x45 { is_float = true; self.advance(); }
      else if c == 0x2D || c == 0x2B { self.advance(); } // sign in exponent
      else { break; }
    }
    let num_str = self.input.slice(start, self.pos);
    if is_float {
      return Result::Ok(TomlValue::Float(parse_f64(num_str)));
    }
    return Result::Ok(TomlValue::Integer(parse_i64(num_str)));
  }

  fn parse_array(refmut<Self>): Result<own<TomlValue>, TomlError> {
    self.expect(0x5B)?;
    let arr: own<Vec<TomlValue>> = Vec::new();
    self.skip_whitespace_and_newlines();
    if self.pos < self.input.len() && self.peek() == 0x5D {
      self.advance();
      return Result::Ok(TomlValue::Array(arr));
    }
    loop {
      self.skip_whitespace_and_newlines();
      self.skip_comment();
      self.skip_whitespace_and_newlines();
      let val = self.parse_value()?;
      arr.push(val);
      self.skip_whitespace_and_newlines();
      self.skip_comment();
      self.skip_whitespace_and_newlines();
      if self.pos < self.input.len() && self.peek() == 0x2C {
        self.advance();
      }
      self.skip_whitespace_and_newlines();
      if self.pos < self.input.len() && self.peek() == 0x5D {
        self.advance();
        return Result::Ok(TomlValue::Array(arr));
      }
    }
  }

  fn parse_inline_table(refmut<Self>): Result<own<TomlValue>, TomlError> {
    self.expect(0x7B)?;
    let entries: own<Vec<(own<String>, TomlValue)>> = Vec::new();
    self.skip_ws();
    if self.pos < self.input.len() && self.peek() == 0x7D {
      self.advance();
      return Result::Ok(TomlValue::Table(entries));
    }
    loop {
      self.skip_ws();
      let key = self.parse_key()?;
      self.skip_ws();
      self.expect(0x3D)?;
      self.skip_ws();
      let val = self.parse_value()?;
      entries.push((key, val));
      self.skip_ws();
      if self.pos < self.input.len() && self.peek() == 0x2C {
        self.advance();
      } else {
        break;
      }
    }
    self.skip_ws();
    self.expect(0x7D)?;
    return Result::Ok(TomlValue::Table(entries));
  }
}

/// Serialize a TomlValue to a TOML string.
function stringify(value: ref<TomlValue>): own<String> {
  let buf = String::new();
  match value {
    TomlValue::Table(entries) => {
      write_table(refmut buf, entries, "");
    },
    _ => {
      write_value(refmut buf, value);
    },
  }
  return buf;
}

function write_value(buf: refmut<String>, value: ref<TomlValue>): void {
  match value {
    TomlValue::String(s) => {
      buf.push('"' as char);
      buf.push_str(s.as_str()); // TODO: escape
      buf.push('"' as char);
    },
    TomlValue::Integer(v) => buf.push_str(i64_to_string(*v).as_str()),
    TomlValue::Float(v) => buf.push_str(f64_to_string(*v).as_str()),
    TomlValue::Boolean(v) => {
      if *v { buf.push_str("true"); } else { buf.push_str("false"); }
    },
    TomlValue::DateTime(s) => buf.push_str(s.as_str()),
    TomlValue::Array(arr) => {
      buf.push('[' as char);
      let i: usize = 0;
      while i < arr.len() {
        if i > 0 { buf.push_str(", "); }
        write_value(buf, arr.get(i).unwrap());
        i = i + 1;
      }
      buf.push(']' as char);
    },
    TomlValue::Table(entries) => {
      buf.push('{' as char);
      let i: usize = 0;
      while i < entries.len() {
        if i > 0 { buf.push_str(", "); }
        let entry = entries.get(i).unwrap();
        buf.push_str(entry.0.as_str());
        buf.push_str(" = ");
        write_value(buf, ref entry.1);
        i = i + 1;
      }
      buf.push('}' as char);
    },
  }
}

function write_table(buf: refmut<String>, entries: ref<Vec<(own<String>, TomlValue)>>, prefix: ref<str>): void {
  // First pass: write non-table values.
  let i: usize = 0;
  while i < entries.len() {
    let entry = entries.get(i).unwrap();
    match entry.1 {
      TomlValue::Table(_) => {},
      _ => {
        buf.push_str(entry.0.as_str());
        buf.push_str(" = ");
        write_value(buf, ref entry.1);
        buf.push(0x0A as char);
      },
    }
    i = i + 1;
  }
  // Second pass: write sub-tables.
  i = 0;
  while i < entries.len() {
    let entry = entries.get(i).unwrap();
    match entry.1 {
      TomlValue::Table(sub) => {
        buf.push(0x0A as char);
        buf.push('[' as char);
        if prefix.len() > 0 {
          buf.push_str(prefix);
          buf.push('.' as char);
        }
        buf.push_str(entry.0.as_str());
        buf.push(']' as char);
        buf.push(0x0A as char);
        let new_prefix = String::new();
        if prefix.len() > 0 {
          new_prefix.push_str(prefix);
          new_prefix.push('.' as char);
        }
        new_prefix.push_str(entry.0.as_str());
        write_table(buf, sub, new_prefix.as_str());
      },
      _ => {},
    }
    i = i + 1;
  }
}
