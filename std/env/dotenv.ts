// std/env/dotenv.ts — .env file loader (RFC-0019 §2.2)

import { HashMap } from '../collections/hashmap';

/// Error type for dotenv loading.
enum DotenvError {
  IoError(own<String>),
  ParseError(usize, own<String>),
}

impl Display for DotenvError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      DotenvError::IoError(msg) => f.write_str("io error: ").and_then(|_| f.write_str(msg.as_str())),
      DotenvError::ParseError(line, msg) => {
        f.write_str("parse error at line ")?;
        line.fmt(f)?;
        f.write_str(": ")?;
        f.write_str(msg.as_str())
      },
    }
  }
}

/// Load a .env file and return a map of key-value pairs.
/// Does NOT set environment variables; caller must do that explicitly.
function load(path: ref<str>): Result<own<HashMap<String, String>>, DotenvError> {
  let content = fs::read_to_string(path);
  if content.is_err() {
    return Result::Err(DotenvError::IoError(String::from("failed to read file")));
  }
  return parse(content.unwrap().as_str());
}

/// Load a .env file and set all variables in the process environment.
/// Existing vars are NOT overwritten — see load_dotenv_override for that.
function load_dotenv(path: ref<str>): Result<void, DotenvError> {
  let map = load(path)?;
  let entries = map.iter();
  while entries.has_next() {
    let entry = entries.next().unwrap();
    // Respect RFC: don't override existing vars.
    match env::var(entry.key.as_str()) {
      Option::Some(_) => { /* skip */ },
      Option::None => { env::set_var(entry.key.as_str(), entry.value.as_str()); },
    }
  }
  return Result::Ok(void);
}

/// Load a .env file and set all variables, overriding existing ones.
function load_dotenv_override(path: ref<str>): Result<void, DotenvError> {
  let map = load(path)?;
  let entries = map.iter();
  while entries.has_next() {
    let entry = entries.next().unwrap();
    env::set_var(entry.key.as_str(), entry.value.as_str());
  }
  return Result::Ok(void);
}

/// Back-compat: set from the dotenv file unconditionally.
function load_and_set(path: ref<str>): Result<void, DotenvError> {
  return load_dotenv_override(path);
}

/// Parse .env content from a string.
/// Supports:
///   KEY=value                 — unquoted, inline "space + #" trims a comment.
///   KEY="a b c"               — double-quoted, honors \n \t \r \\ \" escapes.
///   KEY='raw'                 — single-quoted, all bytes are literal.
///   export KEY=value          — leading `export` keyword is ignored.
///   # comment                 — comment line, skipped.
///   empty lines               — skipped.
function parse(content: ref<str>): Result<own<HashMap<String, String>>, DotenvError> {
  let map: own<HashMap<String, String>> = HashMap::new();
  let lines = content.split('\n');
  let line_num: usize = 1;
  let i: usize = 0;

  while i < lines.len() {
    let raw_line = lines.get(i).unwrap().as_str();
    let line = trim(raw_line);

    // Skip empty lines and full-line comments.
    if line.len() == 0 || line.as_bytes()[0] == 0x23 { // '#'
      i = i + 1;
      line_num = line_num + 1;
      continue;
    }

    // Strip leading `export ` if present (bash-compatible).
    let stripped = strip_export(line);

    // Find '=' separator.
    let eq_pos = find_char(stripped, 0x3D); // '='
    if eq_pos.is_none() {
      return Result::Err(DotenvError::ParseError(line_num, String::from("missing '='")));
    }
    let pos = eq_pos.unwrap();

    let key_slice = trim(stripped.slice(0, pos));
    if !is_valid_key(key_slice) {
      return Result::Err(DotenvError::ParseError(line_num, String::from("invalid key")));
    }

    let raw_value = stripped.slice(pos + 1, stripped.len());
    let value = parse_value(raw_value);

    map.insert(String::from(key_slice), value);
    i = i + 1;
    line_num = line_num + 1;
  }

  return Result::Ok(map);
}

/// Parse a .env file from disk without touching the environment.
function parse_dotenv(content: ref<str>): Result<own<HashMap<String, String>>, DotenvError> {
  return parse(content);
}

/// Parse a .env value, handling quotes, escapes, and inline comments.
function parse_value(raw: ref<str>): own<String> {
  let s = trim(raw);
  let bytes = s.as_bytes();
  let len = bytes.len();
  if len == 0 { return String::new(); }

  let first = bytes[0];

  // Double-quoted: honor escape sequences.
  if first == 0x22 && len >= 2 && bytes[len - 1] == 0x22 {
    return unescape_double_quoted(s.slice(1, len - 1));
  }

  // Single-quoted: literal, no escapes, no interpolation.
  if first == 0x27 && len >= 2 && bytes[len - 1] == 0x27 {
    return String::from(s.slice(1, len - 1));
  }

  // Unquoted: strip inline comments (space + #).
  let i: usize = 0;
  while i < len {
    if bytes[i] == 0x20 && i + 1 < len && bytes[i + 1] == 0x23 {
      return String::from(trim(s.slice(0, i)));
    }
    i = i + 1;
  }

  return String::from(s);
}

/// Decode standard backslash escapes inside a double-quoted .env value.
function unescape_double_quoted(s: ref<str>): own<String> {
  let out: own<String> = String::new();
  let bytes = s.as_bytes();
  let len = bytes.len();
  let i: usize = 0;
  while i < len {
    let c = bytes[i];
    if c == 0x5C && i + 1 < len { // '\'
      let next = bytes[i + 1];
      if next == 0x6E { out.push_byte(0x0A); }       // \n
      else if next == 0x74 { out.push_byte(0x09); }  // \t
      else if next == 0x72 { out.push_byte(0x0D); }  // \r
      else if next == 0x5C { out.push_byte(0x5C); }  // \\
      else if next == 0x22 { out.push_byte(0x22); }  // \"
      else if next == 0x27 { out.push_byte(0x27); }  // \'
      else if next == 0x30 { out.push_byte(0x00); }  // \0
      else {
        // Unknown escape — preserve both bytes verbatim.
        out.push_byte(c);
        out.push_byte(next);
      }
      i = i + 2;
    } else {
      out.push_byte(c);
      i = i + 1;
    }
  }
  return out;
}

/// Strip leading `export ` or `export\t`, otherwise return `s` unchanged.
function strip_export(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let len = bytes.len();
  if len < 7 { return s; }
  if bytes[0] != 0x65 || bytes[1] != 0x78 || bytes[2] != 0x70
     || bytes[3] != 0x6F || bytes[4] != 0x72 || bytes[5] != 0x74 {
    return s;
  }
  let sep = bytes[6];
  if sep != 0x20 && sep != 0x09 { return s; }
  let start: usize = 7;
  while start < len && (bytes[start] == 0x20 || bytes[start] == 0x09) {
    start = start + 1;
  }
  return s.slice(start, len);
}

/// A valid .env key: non-empty, first char [A-Za-z_], rest [A-Za-z0-9_].
function is_valid_key(s: ref<str>): bool {
  let bytes = s.as_bytes();
  let len = bytes.len();
  if len == 0 { return false; }
  let c = bytes[0];
  let is_alpha = (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A) || c == 0x5F;
  if !is_alpha { return false; }
  let i: usize = 1;
  while i < len {
    let ci = bytes[i];
    let is_alnum = (ci >= 0x41 && ci <= 0x5A) || (ci >= 0x61 && ci <= 0x7A)
                   || (ci >= 0x30 && ci <= 0x39) || ci == 0x5F;
    if !is_alnum { return false; }
    i = i + 1;
  }
  return true;
}

function trim(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let len = bytes.len();
  let start: usize = 0;
  while start < len && is_ws(bytes[start]) { start = start + 1; }
  let end = len;
  while end > start && is_ws(bytes[end - 1]) { end = end - 1; }
  return s.slice(start, end);
}

function is_ws(c: u8): bool {
  return c == 0x20 || c == 0x09 || c == 0x0D;
}

function find_char(s: ref<str>, c: u8): Option<usize> {
  let bytes = s.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    if bytes[i] == c { return Option::Some(i); }
    i = i + 1;
  }
  return Option::None;
}
