// std/env/dotenv.ts — .env file loader (RFC-0019)

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
function load_and_set(path: ref<str>): Result<void, DotenvError> {
  let map = load(path)?;
  let entries = map.iter();
  while entries.has_next() {
    let entry = entries.next().unwrap();
    env::set(entry.key.as_str(), entry.value.as_str());
  }
  return Result::Ok(void);
}

/// Parse .env content from a string.
function parse(content: ref<str>): Result<own<HashMap<String, String>>, DotenvError> {
  let map: own<HashMap<String, String>> = HashMap::new();
  let lines = content.split('\n');
  let line_num: usize = 1;
  let i: usize = 0;

  while i < lines.len() {
    let line = lines.get(i).unwrap().as_str();
    let trimmed = trim(line);

    // Skip empty lines and comments.
    if trimmed.len() == 0 || trimmed.as_bytes()[0] == 0x23 { // '#'
      i = i + 1;
      line_num = line_num + 1;
      continue;
    }

    // Find '=' separator.
    let eq_pos = find_char(trimmed, 0x3D); // '='
    if eq_pos.is_none() {
      return Result::Err(DotenvError::ParseError(line_num, String::from("missing '='")));
    }
    let pos = eq_pos.unwrap();

    let key = trim(trimmed.slice(0, pos));
    let raw_value = trimmed.slice(pos + 1, trimmed.len());
    let value = parse_value(raw_value);

    map.insert(String::from(key), value);
    i = i + 1;
    line_num = line_num + 1;
  }

  return Result::Ok(map);
}

/// Parse a .env value, handling quotes and inline comments.
function parse_value(raw: ref<str>): own<String> {
  let s = trim(raw);
  let bytes = s.as_bytes();
  let len = bytes.len();
  if len == 0 { return String::new(); }

  // Check for quoted value.
  let first = bytes[0];
  if (first == 0x22 || first == 0x27) && len >= 2 && bytes[len - 1] == first {
    // Strip quotes.
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
