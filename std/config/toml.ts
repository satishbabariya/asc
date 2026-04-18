// std/config/toml.ts — TOML parser/serializer (RFC-0019)

/// Represents a TOML value.
enum TomlValue {
  String(own<String>),
  Integer(i64),
  Float(f64),
  Boolean(bool),
  Datetime(own<String>),
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

/// Encode a Unicode codepoint as UTF-8 into `out`.
function encode_utf8(cp: u32, out: refmut<String>): void {
  if cp < 0x80 {
    out.push(cp as char);
  } else if cp < 0x800 {
    out.push(((cp >> 6) | 0xC0) as char);
    out.push(((cp & 0x3F) | 0x80) as char);
  } else if cp < 0x10000 {
    out.push(((cp >> 12) | 0xE0) as char);
    out.push((((cp >> 6) & 0x3F) | 0x80) as char);
    out.push(((cp & 0x3F) | 0x80) as char);
  } else {
    out.push(((cp >> 18) | 0xF0) as char);
    out.push((((cp >> 12) & 0x3F) | 0x80) as char);
    out.push((((cp >> 6) & 0x3F) | 0x80) as char);
    out.push(((cp & 0x3F) | 0x80) as char);
  }
}

/// Find or create a child table named `key` in `entries`. Returns a refmut to the
/// child's own entries vector. Rejects duplicates that aren't already tables.
function navigate_table(entries: refmut<Vec<(own<String>, TomlValue)>>, key: ref<str>)
  : Result<refmut<Vec<(own<String>, TomlValue)>>, TomlError> {
  let i: usize = 0;
  while i < entries.len() {
    let entry = entries.get_mut(i).unwrap();
    if entry.0.as_str() == key {
      match entry.1 {
        TomlValue::Table(sub) => { return Result::Ok(refmut sub); },
        TomlValue::Array(arr) => {
          // Array-of-tables: navigate into the last element's table.
          if arr.len() == 0 {
            return Result::Err(TomlError::DuplicateKey(String::from(key)));
          }
          let last = arr.get_mut(arr.len() - 1).unwrap();
          match last {
            TomlValue::Table(sub) => { return Result::Ok(refmut sub); },
            _ => { return Result::Err(TomlError::DuplicateKey(String::from(key))); },
          }
        },
        _ => { return Result::Err(TomlError::DuplicateKey(String::from(key))); },
      }
    }
    i = i + 1;
  }
  // Create a new empty sub-table.
  let new_sub: own<Vec<(own<String>, TomlValue)>> = Vec::new();
  entries.push((String::from(key), TomlValue::Table(new_sub)));
  let last = entries.get_mut(entries.len() - 1).unwrap();
  match last.1 {
    TomlValue::Table(sub) => { return Result::Ok(refmut sub); },
    _ => { return Result::Err(TomlError::InvalidValue(0)); },
  }
}

/// Walk a dotted key path through intermediate tables (created as needed), but
/// do not create the final segment. Returns the parent of the final key.
function walk_parents(root: refmut<Vec<(own<String>, TomlValue)>>, path: ref<Vec<own<String>>>)
  : Result<refmut<Vec<(own<String>, TomlValue)>>, TomlError> {
  let cur = refmut root;
  let i: usize = 0;
  while i + 1 < path.len() {
    let seg = path.get(i).unwrap();
    cur = navigate_table(cur, seg.as_str())?;
    i = i + 1;
  }
  return Result::Ok(cur);
}

/// Insert a `key = value` pair into a table, rejecting duplicates.
function insert_unique(table: refmut<Vec<(own<String>, TomlValue)>>, key: own<String>, value: TomlValue)
  : Result<void, TomlError> {
  let i: usize = 0;
  while i < table.len() {
    let entry = table.get(i).unwrap();
    if entry.0.as_str() == key.as_str() {
      return Result::Err(TomlError::DuplicateKey(key));
    }
    i = i + 1;
  }
  table.push((key, value));
  return Result::Ok(void);
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
    if c == 0x5B { // '[' table header, possibly '[[' for array-of-tables
      parser.advance();
      let is_array = false;
      if parser.pos < parser.input.len() && parser.peek() == 0x5B {
        is_array = true;
        parser.advance();
      }
      parser.skip_ws();
      let path = parser.parse_key_path()?;
      parser.skip_ws();
      if is_array {
        parser.expect(0x5D)?;
        parser.expect(0x5D)?;
      } else {
        parser.expect(0x5D)?;
      }
      if path.len() == 0 {
        return Result::Err(TomlError::UnexpectedToken(parser.line, String::from("empty table header")));
      }
      // Walk intermediate segments (creating tables as needed).
      let parent = walk_parents(refmut root, ref path)?;
      let last_name = path.get(path.len() - 1).unwrap();
      if is_array {
        // Array-of-tables: append a fresh table to (or create) the array at last_name.
        let new_tbl: own<Vec<(own<String>, TomlValue)>> = Vec::new();
        let found = false;
        let i: usize = 0;
        while i < parent.len() {
          let entry = parent.get_mut(i).unwrap();
          if entry.0.as_str() == last_name.as_str() {
            match entry.1 {
              TomlValue::Array(arr) => {
                arr.push(TomlValue::Table(new_tbl));
                let last = arr.get_mut(arr.len() - 1).unwrap();
                match last {
                  TomlValue::Table(sub) => { current_table = refmut sub; },
                  _ => {},
                }
                found = true;
              },
              _ => { return Result::Err(TomlError::DuplicateKey(String::from(last_name.as_str()))); },
            }
            break;
          }
          i = i + 1;
        }
        if !found {
          let new_arr: own<Vec<TomlValue>> = Vec::new();
          new_arr.push(TomlValue::Table(new_tbl));
          parent.push((String::from(last_name.as_str()), TomlValue::Array(new_arr)));
          let added = parent.get_mut(parent.len() - 1).unwrap();
          match added.1 {
            TomlValue::Array(arr) => {
              let last = arr.get_mut(arr.len() - 1).unwrap();
              match last {
                TomlValue::Table(sub) => { current_table = refmut sub; },
                _ => {},
              }
            },
            _ => {},
          }
        }
      } else {
        // Regular table header: find-or-create; duplicates with non-table are errors.
        current_table = navigate_table(parent, last_name.as_str())?;
      }
      continue;
    }

    // Key = Value pair (possibly dotted).
    let kpath = parser.parse_key_path()?;
    parser.skip_ws();
    parser.expect(0x3D)?; // '='
    parser.skip_ws();
    let value = parser.parse_value()?;
    // Descend into sub-tables for dotted keys; final segment is the inserted key.
    let target = walk_parents(refmut current_table, ref kpath)?;
    let last_name = kpath.get(kpath.len() - 1).unwrap();
    insert_unique(target, String::from(last_name.as_str()), value)?;
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

  /// Parse a dotted key path (e.g. `a.b.c`). Returns at least one segment.
  fn parse_key_path(refmut<Self>): Result<own<Vec<own<String>>>, TomlError> {
    let path: own<Vec<own<String>>> = Vec::new();
    let seg = self.parse_key()?;
    path.push(seg);
    loop {
      self.skip_ws();
      if self.pos < self.input.len() && self.peek() == 0x2E { // '.'
        self.advance();
        self.skip_ws();
        let next = self.parse_key()?;
        path.push(next);
      } else {
        break;
      }
    }
    return Result::Ok(path);
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
    // Datetime / date / time start with digits; disambiguate from plain numbers
    // by lookahead for ISO 8601 punctuation (`-` after 4 digits, `:` after 2 digits).
    if self.looks_like_datetime() {
      return self.parse_datetime();
    }
    // Number (integer or float).
    return self.parse_number();
  }

  /// Returns true if the current position starts an RFC 3339 date, time, or
  /// datetime (local or offset). Does not consume input.
  fn looks_like_datetime(ref<Self>): bool {
    // Need at least 5 bytes of lookahead to tell apart from a number.
    let bytes = self.input.as_bytes();
    let n = self.input.len();
    let p = self.pos;
    // YYYY-
    if p + 4 < n
      && bytes[p] >= 0x30 && bytes[p] <= 0x39
      && bytes[p + 1] >= 0x30 && bytes[p + 1] <= 0x39
      && bytes[p + 2] >= 0x30 && bytes[p + 2] <= 0x39
      && bytes[p + 3] >= 0x30 && bytes[p + 3] <= 0x39
      && bytes[p + 4] == 0x2D {
      return true;
    }
    // HH:
    if p + 2 < n
      && bytes[p] >= 0x30 && bytes[p] <= 0x39
      && bytes[p + 1] >= 0x30 && bytes[p + 1] <= 0x39
      && bytes[p + 2] == 0x3A {
      return true;
    }
    return false;
  }

  /// Parse an RFC 3339 datetime, local datetime, local date, or local time.
  /// The grammar is permissive: we accept the characters that may appear in
  /// any of those variants (digits, `-`, `:`, `.`, `T`, `t`, space, `Z`, `z`,
  /// `+`) and stop at the first character that cannot.
  fn parse_datetime(refmut<Self>): Result<own<TomlValue>, TomlError> {
    let start = self.pos;
    let saw_date = false;
    let saw_time = false;
    // Date part: YYYY-MM-DD.
    if self.pos + 10 <= self.input.len() {
      let b = self.input.as_bytes();
      if b[self.pos + 4] == 0x2D {
        self.pos = self.pos + 10;
        saw_date = true;
      }
    }
    // Optional date-time separator: T, t, or space (only if followed by HH:).
    if saw_date && self.pos + 3 <= self.input.len() {
      let b = self.input.as_bytes();
      let sep = b[self.pos];
      if sep == 0x54 || sep == 0x74 || sep == 0x20 {
        // Require HH:MM after separator to keep `date then space then key` safe.
        if self.pos + 3 < self.input.len()
          && b[self.pos + 1] >= 0x30 && b[self.pos + 1] <= 0x39
          && b[self.pos + 2] >= 0x30 && b[self.pos + 2] <= 0x39
          && b[self.pos + 3] == 0x3A {
          self.advance();
          saw_time = true;
        }
      }
    }
    if !saw_date { saw_time = true; } // time-only case
    // Time part: HH:MM:SS[.frac]
    if saw_time {
      while self.pos < self.input.len() {
        let c = self.peek();
        if (c >= 0x30 && c <= 0x39) || c == 0x3A || c == 0x2E {
          self.advance();
        } else { break; }
      }
      // Optional timezone: Z | z | (+|-)HH:MM
      if self.pos < self.input.len() {
        let c = self.peek();
        if c == 0x5A || c == 0x7A {
          self.advance();
        } else if c == 0x2B || (c == 0x2D && saw_date) {
          // Only treat `-` as offset sign if we already saw a date (else it
          // could be part of an unreachable expression).
          self.advance();
          while self.pos < self.input.len() {
            let cc = self.peek();
            if (cc >= 0x30 && cc <= 0x39) || cc == 0x3A { self.advance(); }
            else { break; }
          }
        }
      }
    }
    let s = String::from(self.input.slice(start, self.pos));
    return Result::Ok(TomlValue::Datetime(s));
  }

  /// Parse a TOML escape sequence (the char after '\\' already consumed as `esc`).
  /// Supports: \b \t \n \f \r \" \\ \uXXXX \UXXXXXXXX.
  fn apply_escape(refmut<Self>, esc: u8, result: refmut<String>): Result<void, TomlError> {
    if esc == 0x62 { result.push(0x08 as char); return Result::Ok(void); }          // \b
    if esc == 0x74 { result.push(0x09 as char); return Result::Ok(void); }          // \t
    if esc == 0x6E { result.push(0x0A as char); return Result::Ok(void); }          // \n
    if esc == 0x66 { result.push(0x0C as char); return Result::Ok(void); }          // \f
    if esc == 0x72 { result.push(0x0D as char); return Result::Ok(void); }          // \r
    if esc == 0x22 { result.push('"' as char); return Result::Ok(void); }           // \"
    if esc == 0x5C { result.push('\\' as char); return Result::Ok(void); }          // \\
    if esc == 0x2F { result.push('/' as char); return Result::Ok(void); }           // \/ (allowed)
    if esc == 0x75 { return self.parse_unicode_escape(4, result); }                  // \uXXXX
    if esc == 0x55 { return self.parse_unicode_escape(8, result); }                  // \UXXXXXXXX
    return Result::Err(TomlError::UnexpectedToken(self.line, String::from("invalid escape sequence")));
  }

  /// Parse `n` hex digits and emit the resulting codepoint as UTF-8.
  fn parse_unicode_escape(refmut<Self>, n: usize, result: refmut<String>): Result<void, TomlError> {
    if self.pos + n > self.input.len() {
      return Result::Err(TomlError::UnexpectedEof);
    }
    let cp: u32 = 0;
    let i: usize = 0;
    while i < n {
      let h = self.advance();
      let digit: u32 = 0;
      if h >= 0x30 && h <= 0x39 { digit = (h - 0x30) as u32; }
      else if h >= 0x41 && h <= 0x46 { digit = (h - 0x41 + 10) as u32; }
      else if h >= 0x61 && h <= 0x66 { digit = (h - 0x61 + 10) as u32; }
      else { return Result::Err(TomlError::UnexpectedToken(self.line, String::from("invalid hex digit in unicode escape"))); }
      cp = (cp << 4) | digit;
      i = i + 1;
    }
    encode_utf8(cp, result);
    return Result::Ok(void);
  }

  fn parse_basic_string(refmut<Self>): Result<own<String>, TomlError> {
    self.expect(0x22)?;
    // Check for multi-line basic string: """
    if self.pos + 1 < self.input.len()
      && self.input.as_bytes()[self.pos] == 0x22
      && self.input.as_bytes()[self.pos + 1] == 0x22 {
      self.advance();
      self.advance();
      return self.parse_multiline_basic_string();
    }
    let result = String::new();
    while self.pos < self.input.len() {
      let c = self.advance();
      if c == 0x22 { return Result::Ok(result); }
      if c == 0x0A {
        return Result::Err(TomlError::UnexpectedToken(self.line, String::from("newline in basic string")));
      }
      if c == 0x5C { // escape
        if self.pos >= self.input.len() { return Result::Err(TomlError::UnexpectedEof); }
        let esc = self.advance();
        self.apply_escape(esc, refmut result)?;
      } else {
        result.push(c as char);
      }
    }
    return Result::Err(TomlError::UnexpectedEof);
  }

  /// Multi-line basic string: content between """ and """. Supports line-ending
  /// backslash (trim trailing whitespace + newline) and all escape sequences.
  fn parse_multiline_basic_string(refmut<Self>): Result<own<String>, TomlError> {
    let result = String::new();
    // A newline immediately after the opening """ is trimmed.
    if self.pos < self.input.len() && self.peek() == 0x0D { self.advance(); }
    if self.pos < self.input.len() && self.peek() == 0x0A { self.advance(); }
    while self.pos < self.input.len() {
      // Check for closing """
      if self.peek() == 0x22
        && self.pos + 2 < self.input.len()
        && self.input.as_bytes()[self.pos + 1] == 0x22
        && self.input.as_bytes()[self.pos + 2] == 0x22 {
        self.advance();
        self.advance();
        self.advance();
        // TOML allows up to two extra quotes before the closing delim.
        if self.pos < self.input.len() && self.peek() == 0x22 {
          result.push('"' as char);
          self.advance();
          if self.pos < self.input.len() && self.peek() == 0x22 {
            result.push('"' as char);
            self.advance();
          }
        }
        return Result::Ok(result);
      }
      let c = self.advance();
      if c == 0x5C {
        if self.pos >= self.input.len() { return Result::Err(TomlError::UnexpectedEof); }
        let esc = self.peek();
        // Line-ending backslash: trim trailing ws + newline until next non-ws.
        if esc == 0x0A || esc == 0x0D || esc == 0x20 || esc == 0x09 {
          // Peek: is this a line-ending backslash? (i.e. only ws until newline)
          let save = self.pos;
          let mut_ok = true;
          while mut_ok && self.pos < self.input.len() {
            let p = self.peek();
            if p == 0x20 || p == 0x09 { self.advance(); }
            else if p == 0x0A { self.advance(); break; }
            else if p == 0x0D { self.advance(); if self.pos < self.input.len() && self.peek() == 0x0A { self.advance(); } break; }
            else { mut_ok = false; self.pos = save; break; }
          }
          if mut_ok {
            // Trim subsequent whitespace.
            while self.pos < self.input.len() {
              let p = self.peek();
              if p == 0x20 || p == 0x09 || p == 0x0A || p == 0x0D { self.advance(); }
              else { break; }
            }
            continue;
          }
        }
        self.advance(); // consume the escape char
        self.apply_escape(esc, refmut result)?;
      } else {
        result.push(c as char);
      }
    }
    return Result::Err(TomlError::UnexpectedEof);
  }

  fn parse_literal_string(refmut<Self>): Result<own<String>, TomlError> {
    self.expect(0x27)?;
    // Check for multi-line literal string: '''
    if self.pos + 1 < self.input.len()
      && self.input.as_bytes()[self.pos] == 0x27
      && self.input.as_bytes()[self.pos + 1] == 0x27 {
      self.advance();
      self.advance();
      return self.parse_multiline_literal_string();
    }
    let start = self.pos;
    while self.pos < self.input.len() {
      if self.peek() == 0x27 {
        let s = String::from(self.input.slice(start, self.pos));
        self.advance();
        return Result::Ok(s);
      }
      if self.peek() == 0x0A {
        return Result::Err(TomlError::UnexpectedToken(self.line, String::from("newline in literal string")));
      }
      self.advance();
    }
    return Result::Err(TomlError::UnexpectedEof);
  }

  /// Multi-line literal string: raw content between ''' and ''', no escapes.
  fn parse_multiline_literal_string(refmut<Self>): Result<own<String>, TomlError> {
    // A newline immediately after the opening ''' is trimmed.
    if self.pos < self.input.len() && self.peek() == 0x0D { self.advance(); }
    if self.pos < self.input.len() && self.peek() == 0x0A { self.advance(); }
    let start = self.pos;
    while self.pos < self.input.len() {
      if self.peek() == 0x27
        && self.pos + 2 < self.input.len()
        && self.input.as_bytes()[self.pos + 1] == 0x27
        && self.input.as_bytes()[self.pos + 2] == 0x27 {
        let end = self.pos;
        self.advance();
        self.advance();
        self.advance();
        let s = String::from(self.input.slice(start, end));
        // TOML allows up to two extra apostrophes before the closing delim.
        if self.pos < self.input.len() && self.peek() == 0x27 {
          s.push('\'' as char);
          self.advance();
          if self.pos < self.input.len() && self.peek() == 0x27 {
            s.push('\'' as char);
            self.advance();
          }
        }
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

/// Write `s` to `buf` with TOML basic-string escapes applied.
function write_escaped_string(buf: refmut<String>, s: ref<str>): void {
  let bytes = s.as_bytes();
  let n = s.len();
  let i: usize = 0;
  while i < n {
    let c = bytes[i];
    if c == 0x22 { buf.push('\\' as char); buf.push('"' as char); }
    else if c == 0x5C { buf.push('\\' as char); buf.push('\\' as char); }
    else if c == 0x08 { buf.push('\\' as char); buf.push('b' as char); }
    else if c == 0x09 { buf.push('\\' as char); buf.push('t' as char); }
    else if c == 0x0A { buf.push('\\' as char); buf.push('n' as char); }
    else if c == 0x0C { buf.push('\\' as char); buf.push('f' as char); }
    else if c == 0x0D { buf.push('\\' as char); buf.push('r' as char); }
    else if c < 0x20 {
      // \uXXXX form for other control chars.
      buf.push('\\' as char);
      buf.push('u' as char);
      let j: i32 = 3;
      while j >= 0 {
        let nib = ((c as u32) >> (j * 4)) & 0xF;
        let ch: u8 = 0;
        if nib < 10 { ch = (nib + 0x30) as u8; } else { ch = (nib - 10 + 0x41) as u8; }
        buf.push(ch as char);
        j = j - 1;
      }
    }
    else { buf.push(c as char); }
    i = i + 1;
  }
}

function write_value(buf: refmut<String>, value: ref<TomlValue>): void {
  match value {
    TomlValue::String(s) => {
      buf.push('"' as char);
      write_escaped_string(buf, s.as_str());
      buf.push('"' as char);
    },
    TomlValue::Integer(v) => buf.push_str(i64_to_string(*v).as_str()),
    TomlValue::Float(v) => buf.push_str(f64_to_string(*v).as_str()),
    TomlValue::Boolean(v) => {
      if *v { buf.push_str("true"); } else { buf.push_str("false"); }
    },
    TomlValue::Datetime(s) => buf.push_str(s.as_str()),
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

/// Return true if `arr` is an array whose elements are all Tables — i.e.
/// it should be serialized as `[[name]]` array-of-tables blocks.
function is_array_of_tables(arr: ref<Vec<TomlValue>>): bool {
  if arr.len() == 0 { return false; }
  let i: usize = 0;
  while i < arr.len() {
    match arr.get(i).unwrap() {
      TomlValue::Table(_) => {},
      _ => { return false; },
    }
    i = i + 1;
  }
  return true;
}

function write_table(buf: refmut<String>, entries: ref<Vec<(own<String>, TomlValue)>>, prefix: ref<str>): void {
  // First pass: write scalar / non-table / non-array-of-tables values.
  let i: usize = 0;
  while i < entries.len() {
    let entry = entries.get(i).unwrap();
    match entry.1 {
      TomlValue::Table(_) => {},
      TomlValue::Array(arr) => {
        if is_array_of_tables(arr) {
          // Deferred to the next pass so headers appear after scalars.
        } else {
          buf.push_str(entry.0.as_str());
          buf.push_str(" = ");
          write_value(buf, ref entry.1);
          buf.push(0x0A as char);
        }
      },
      _ => {
        buf.push_str(entry.0.as_str());
        buf.push_str(" = ");
        write_value(buf, ref entry.1);
        buf.push(0x0A as char);
      },
    }
    i = i + 1;
  }
  // Second pass: write sub-tables and array-of-tables.
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
      TomlValue::Array(arr) => {
        if is_array_of_tables(arr) {
          let new_prefix = String::new();
          if prefix.len() > 0 {
            new_prefix.push_str(prefix);
            new_prefix.push('.' as char);
          }
          new_prefix.push_str(entry.0.as_str());
          let j: usize = 0;
          while j < arr.len() {
            buf.push(0x0A as char);
            buf.push('[' as char);
            buf.push('[' as char);
            buf.push_str(new_prefix.as_str());
            buf.push(']' as char);
            buf.push(']' as char);
            buf.push(0x0A as char);
            match arr.get(j).unwrap() {
              TomlValue::Table(sub) => { write_table(buf, sub, new_prefix.as_str()); },
              _ => {},
            }
            j = j + 1;
          }
        }
      },
      _ => {},
    }
    i = i + 1;
  }
}
