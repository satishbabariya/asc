// std/config/ini.ts — INI format parser/serializer (RFC-0019 §3.3)
//
// Supports:
//   - Configurable comment characters (default ';' and '#')
//   - Configurable assignment characters (default '=' and ':')
//   - Optional multiline values via leading-whitespace continuation lines
//   - Optional ${var} interpolation (either same-section or "section.key")
//   - Case-sensitive/insensitive key matching
//   - Quoted string values with basic escapes (\\, \", \n, \r, \t)
//   - Order-preserving round trip (Vec-of-pairs storage)

/// Represents an INI file as sections containing key-value pairs.
/// The empty-string section holds top-level (sectionless) entries.
struct IniDocument {
  sections: own<Vec<IniSection>>,
  case_sensitive: bool,
}

struct IniSection {
  name: own<String>,
  entries: own<Vec<(own<String>, own<String>)>>,
}

/// Parser options. A `new()` helper returns the defaults documented in
/// RFC-0019 §3.3.
struct ParseOptions {
  comment_chars: own<String>,
  assignment_chars: own<String>,
  allow_no_value: bool,
  multiline: bool,
  interpolation: bool,
  case_sensitive_keys: bool,
}

impl ParseOptions {
  fn new(): own<ParseOptions> {
    return ParseOptions {
      comment_chars: String::from(";#"),
      assignment_chars: String::from("=:"),
      allow_no_value: false,
      multiline: false,
      interpolation: false,
      case_sensitive_keys: true,
    };
  }
}

/// Error type for INI parsing.
enum IniError {
  InvalidLine(usize, own<String>),
  UnclosedSection(usize),
  InvalidKey(usize),
  MissingSeparator(usize),
  InterpolationError(usize, own<String>),
  UnterminatedString(usize),
}

impl Display for IniError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      IniError::InvalidLine(line, msg) => {
        f.write_str("line ")?; line.fmt(f)?; f.write_str(": ")?; f.write_str(msg.as_str())
      },
      IniError::UnclosedSection(line) => f.write_str("unclosed section at line ").and_then(|_| line.fmt(f)),
      IniError::InvalidKey(line) => f.write_str("invalid key at line ").and_then(|_| line.fmt(f)),
      IniError::MissingSeparator(line) => f.write_str("missing separator at line ").and_then(|_| line.fmt(f)),
      IniError::InterpolationError(line, key) => {
        f.write_str("line ")?; line.fmt(f)?; f.write_str(": unresolved interpolation for '")?;
        f.write_str(key.as_str())?; f.write_str("'")
      },
      IniError::UnterminatedString(line) => f.write_str("unterminated string at line ").and_then(|_| line.fmt(f)),
    }
  }
}

impl IniDocument {
  fn new(): own<IniDocument> {
    let sections: own<Vec<IniSection>> = Vec::new();
    sections.push(IniSection { name: String::new(), entries: Vec::new() });
    return IniDocument { sections: sections, case_sensitive: true };
  }

  /// Get a value by section and key.
  fn get(ref<Self>, section: ref<str>, key: ref<str>): Option<ref<str>> {
    let i: usize = 0;
    while i < self.sections.len() {
      let sec = self.sections.get(i).unwrap();
      if keys_equal(sec.name.as_str(), section, self.case_sensitive) {
        let j: usize = 0;
        while j < sec.entries.len() {
          let entry = sec.entries.get(j).unwrap();
          if keys_equal(entry.0.as_str(), key, self.case_sensitive) {
            return Option::Some(entry.1.as_str());
          }
          j = j + 1;
        }
      }
      i = i + 1;
    }
    return Option::None;
  }

  /// Set a value (creates section if needed). Preserves insertion order.
  fn set(refmut<Self>, section: ref<str>, key: ref<str>, value: ref<str>): void {
    let sec_idx: Option<usize> = Option::None;
    let i: usize = 0;
    while i < self.sections.len() {
      if keys_equal(self.sections.get(i).unwrap().name.as_str(), section, self.case_sensitive) {
        sec_idx = Option::Some(i);
        break;
      }
      i = i + 1;
    }
    if sec_idx.is_none() {
      self.sections.push(IniSection {
        name: String::from(section),
        entries: Vec::new(),
      });
      sec_idx = Option::Some(self.sections.len() - 1);
    }
    let sec = self.sections.get_mut(sec_idx.unwrap()).unwrap();
    let j: usize = 0;
    while j < sec.entries.len() {
      if keys_equal(sec.entries.get(j).unwrap().0.as_str(), key, self.case_sensitive) {
        let entry = sec.entries.get_mut(j).unwrap();
        entry.1 = String::from(value);
        return;
      }
      j = j + 1;
    }
    sec.entries.push((String::from(key), String::from(value)));
  }

  /// Get all section names (excluding the sentinel empty-name section).
  fn section_names(ref<Self>): own<Vec<own<String>>> {
    let result: own<Vec<own<String>>> = Vec::new();
    let i: usize = 0;
    while i < self.sections.len() {
      let sec = self.sections.get(i).unwrap();
      if sec.name.len() > 0 {
        result.push(sec.name.clone());
      }
      i = i + 1;
    }
    return result;
  }
}

/// Parse an INI string using default options.
function parse(input: ref<str>): Result<own<IniDocument>, IniError> {
  return parse_with(input, ParseOptions::new());
}

/// Parse an INI string with the supplied options.
function parse_with(input: ref<str>, opts: ref<ParseOptions>): Result<own<IniDocument>, IniError> {
  let doc = IniDocument::new();
  doc.case_sensitive = opts.case_sensitive_keys;
  let current_section: usize = 0;
  let lines = input.split('\n');
  let line_num: usize = 0;

  // For multiline continuation, remember last appended (section, entry).
  let last_section: isize = -1;
  let last_entry: isize = -1;

  let i: usize = 0;
  while i < lines.len() {
    line_num = line_num + 1;
    let raw = lines.get(i).unwrap().as_str();
    let line = trim_right(raw);

    // Multiline continuation: lines that start with whitespace *and* a
    // non-empty, non-comment body append to the previous value.
    if opts.multiline && raw.len() > 0 && is_ws(raw.as_bytes()[0])
        && last_section >= 0 && last_entry >= 0 {
      let trimmed = trim(raw);
      if trimmed.len() > 0 && !is_comment_char(trimmed.as_bytes()[0], opts.comment_chars.as_str()) {
        let sec = doc.sections.get_mut(last_section as usize).unwrap();
        let entry = sec.entries.get_mut(last_entry as usize).unwrap();
        entry.1.push(0x0A as char);
        entry.1.push_str(trimmed);
        i = i + 1;
        continue;
      }
    }

    let line = trim(line);
    if line.len() == 0 {
      i = i + 1;
      continue;
    }
    if is_comment_char(line.as_bytes()[0], opts.comment_chars.as_str()) {
      i = i + 1;
      continue;
    }

    // Section header: [name]
    if line.as_bytes()[0] == 0x5B {
      let end = find_byte(line, 0x5D);
      if end.is_none() {
        return Result::Err(IniError::UnclosedSection(line_num));
      }
      let name = trim(line.slice(1, end.unwrap()));
      if name.len() == 0 {
        return Result::Err(IniError::InvalidLine(line_num, String::from("empty section name")));
      }
      let found = false;
      let j: usize = 0;
      while j < doc.sections.len() {
        if keys_equal(doc.sections.get(j).unwrap().name.as_str(), name, doc.case_sensitive) {
          current_section = j;
          found = true;
          break;
        }
        j = j + 1;
      }
      if !found {
        doc.sections.push(IniSection { name: String::from(name), entries: Vec::new() });
        current_section = doc.sections.len() - 1;
      }
      last_section = -1;
      last_entry = -1;
      i = i + 1;
      continue;
    }

    // Key = Value pair (first occurrence of any assignment char wins).
    let sep = find_any(line, opts.assignment_chars.as_str());
    if sep.is_none() {
      if opts.allow_no_value {
        let key = trim(line);
        if !is_valid_key(key) {
          return Result::Err(IniError::InvalidKey(line_num));
        }
        let sec = doc.sections.get_mut(current_section).unwrap();
        sec.entries.push((String::from(key), String::new()));
        last_section = current_section as isize;
        last_entry = (sec.entries.len() - 1) as isize;
        i = i + 1;
        continue;
      }
      return Result::Err(IniError::MissingSeparator(line_num));
    }
    let key = trim(line.slice(0, sep.unwrap()));
    if !is_valid_key(key) {
      return Result::Err(IniError::InvalidKey(line_num));
    }
    let raw_val = trim(line.slice(sep.unwrap() + 1, line.len()));
    // Strip inline comment if not inside quotes.
    let val_no_comment = strip_inline_comment(raw_val, opts.comment_chars.as_str());
    let processed: own<String> = process_value(val_no_comment, line_num)?;

    let sec = doc.sections.get_mut(current_section).unwrap();
    sec.entries.push((String::from(key), processed));
    last_section = current_section as isize;
    last_entry = (sec.entries.len() - 1) as isize;
    i = i + 1;
  }

  if opts.interpolation {
    interpolate(refmut doc, lines.len())?;
  }

  return Result::Ok(doc);
}

/// Typed deserialization stub.
/// RFC-0016's derive(Deserialize) macro is not yet implemented (see CLAUDE.md
/// "Known Gaps" #7). Call `parse()` and walk the document for now.
function from_str<T>(input: ref<str>): Result<own<IniDocument>, IniError> {
  return parse(input);
}

/// Typed serialization: falls back to `stringify()` until derive(Serialize) lands.
function to_string<T>(doc: ref<IniDocument>): own<String> {
  return stringify(doc);
}

/// Serialize an IniDocument to a string. Preserves section and entry order.
function stringify(doc: ref<IniDocument>): own<String> {
  let buf = String::new();
  let i: usize = 0;
  while i < doc.sections.len() {
    let sec = doc.sections.get(i).unwrap();
    if sec.name.len() > 0 {
      if buf.len() > 0 { buf.push(0x0A as char); }
      buf.push('[' as char);
      buf.push_str(sec.name.as_str());
      buf.push(']' as char);
      buf.push(0x0A as char);
    }
    let j: usize = 0;
    while j < sec.entries.len() {
      let entry = sec.entries.get(j).unwrap();
      buf.push_str(entry.0.as_str());
      buf.push_str(" = ");
      write_value(refmut buf, entry.1.as_str());
      buf.push(0x0A as char);
      j = j + 1;
    }
    i = i + 1;
  }
  return buf;
}

// --- Internal helpers ---

function is_ws(b: u8): bool { return b == 0x20 || b == 0x09; }

function is_comment_char(b: u8, chars: ref<str>): bool {
  let cb = chars.as_bytes();
  let i: usize = 0;
  while i < cb.len() {
    if cb[i] == b { return true; }
    i = i + 1;
  }
  return false;
}

function keys_equal(a: ref<str>, b: ref<str>, case_sensitive: bool): bool {
  if case_sensitive { return a == b; }
  let ab = a.as_bytes();
  let bb = b.as_bytes();
  if ab.len() != bb.len() { return false; }
  let i: usize = 0;
  while i < ab.len() {
    let x = ab[i];
    let y = bb[i];
    if x >= 0x41 && x <= 0x5A { x = x + 0x20; }
    if y >= 0x41 && y <= 0x5A { y = y + 0x20; }
    if x != y { return false; }
    i = i + 1;
  }
  return true;
}

function is_valid_key(k: ref<str>): bool {
  if k.len() == 0 { return false; }
  // Keys can't start with a bracket.
  let first = k.as_bytes()[0];
  if first == 0x5B { return false; }
  return true;
}

function trim(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let start: usize = 0;
  while start < bytes.len() && (bytes[start] == 0x20 || bytes[start] == 0x09 || bytes[start] == 0x0D) {
    start = start + 1;
  }
  let end = bytes.len();
  while end > start && (bytes[end - 1] == 0x20 || bytes[end - 1] == 0x09 || bytes[end - 1] == 0x0D) {
    end = end - 1;
  }
  return s.slice(start, end);
}

function trim_right(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let end = bytes.len();
  while end > 0 && (bytes[end - 1] == 0x20 || bytes[end - 1] == 0x09 || bytes[end - 1] == 0x0D) {
    end = end - 1;
  }
  return s.slice(0, end);
}

function find_byte(s: ref<str>, b: u8): Option<usize> {
  let bytes = s.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    if bytes[i] == b { return Option::Some(i); }
    i = i + 1;
  }
  return Option::None;
}

/// Return the byte index of the first character in `s` matching any byte
/// in `chars`. Respects quoted regions so a ':' inside "..." is ignored.
function find_any(s: ref<str>, chars: ref<str>): Option<usize> {
  let bytes = s.as_bytes();
  let cb = chars.as_bytes();
  let in_double = false;
  let in_single = false;
  let i: usize = 0;
  while i < bytes.len() {
    let b = bytes[i];
    if b == 0x5C && i + 1 < bytes.len() {
      i = i + 2;
      continue;
    }
    if b == 0x22 && !in_single { in_double = !in_double; i = i + 1; continue; }
    if b == 0x27 && !in_double { in_single = !in_single; i = i + 1; continue; }
    if !in_double && !in_single {
      let j: usize = 0;
      while j < cb.len() {
        if cb[j] == b { return Option::Some(i); }
        j = j + 1;
      }
    }
    i = i + 1;
  }
  return Option::None;
}

/// Strip a trailing `# inline comment` or `; inline comment` from `s` but
/// keep comment characters that live inside quoted strings.
function strip_inline_comment(s: ref<str>, comment_chars: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let in_double = false;
  let in_single = false;
  let i: usize = 0;
  while i < bytes.len() {
    let b = bytes[i];
    if b == 0x5C && i + 1 < bytes.len() { i = i + 2; continue; }
    if b == 0x22 && !in_single { in_double = !in_double; i = i + 1; continue; }
    if b == 0x27 && !in_double { in_single = !in_single; i = i + 1; continue; }
    if !in_double && !in_single && is_comment_char(b, comment_chars) {
      // Only strip if preceded by whitespace, so "foo#bar" stays one value.
      if i > 0 && (bytes[i - 1] == 0x20 || bytes[i - 1] == 0x09) {
        return trim(s.slice(0, i));
      }
    }
    i = i + 1;
  }
  return s;
}

/// Decode a value: trim, unquote if fully quoted, expand escape sequences.
function process_value(v: ref<str>, line_num: usize): Result<own<String>, IniError> {
  let s = trim(v);
  let bytes = s.as_bytes();
  let len = bytes.len();
  if len >= 2 {
    let first = bytes[0];
    let last = bytes[len - 1];
    if first == 0x22 && last == 0x22 {
      return decode_escapes(s.slice(1, len - 1), line_num);
    }
    if first == 0x27 && last == 0x27 {
      // Literal quotes: no escape processing.
      return Result::Ok(String::from(s.slice(1, len - 1)));
    }
  }
  return Result::Ok(String::from(s));
}

function decode_escapes(s: ref<str>, line_num: usize): Result<own<String>, IniError> {
  let out = String::new();
  let bytes = s.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    let b = bytes[i];
    if b == 0x5C {
      if i + 1 >= bytes.len() {
        return Result::Err(IniError::UnterminatedString(line_num));
      }
      let e = bytes[i + 1];
      if e == 0x6E { out.push(0x0A as char); }       // \n
      else if e == 0x74 { out.push(0x09 as char); }  // \t
      else if e == 0x72 { out.push(0x0D as char); }  // \r
      else if e == 0x30 { out.push(0x00 as char); }  // \0
      else if e == 0x5C { out.push(0x5C as char); }  // \\
      else if e == 0x22 { out.push(0x22 as char); }  // \"
      else if e == 0x27 { out.push(0x27 as char); }  // \'
      else { out.push(e as char); }                  // pass-through
      i = i + 2;
    } else {
      out.push(b as char);
      i = i + 1;
    }
  }
  return Result::Ok(out);
}

/// Expand ${name} or ${section.name} references in every value. Uses a
/// bounded iteration count to avoid cycles.
function interpolate(doc: refmut<IniDocument>, max_lines: usize): Result<void, IniError> {
  const MAX_PASSES: usize = 16;
  let pass: usize = 0;
  while pass < MAX_PASSES {
    let changed = false;
    let si: usize = 0;
    while si < doc.sections.len() {
      let sec_name: own<String> = doc.sections.get(si).unwrap().name.clone();
      let ei: usize = 0;
      let entries_len = doc.sections.get(si).unwrap().entries.len();
      while ei < entries_len {
        let val_clone: own<String> = doc.sections.get(si).unwrap()
          .entries.get(ei).unwrap().1.clone();
        let expanded = expand_once(ref doc, val_clone.as_str(), sec_name.as_str(), max_lines)?;
        if expanded.0 {
          let sec = doc.sections.get_mut(si).unwrap();
          let entry = sec.entries.get_mut(ei).unwrap();
          entry.1 = expanded.1;
          changed = true;
        }
        ei = ei + 1;
      }
      si = si + 1;
    }
    if !changed { return Result::Ok(void); }
    pass = pass + 1;
  }
  return Result::Ok(void);
}

/// Replace every `${...}` token in `value` with the referenced value.
/// Returns (changed?, new_string). A missing reference is an error.
function expand_once(doc: ref<IniDocument>, value: ref<str>, current_section: ref<str>,
                    line_num: usize): Result<(bool, own<String>), IniError> {
  let out = String::new();
  let bytes = value.as_bytes();
  let changed = false;
  let i: usize = 0;
  while i < bytes.len() {
    if bytes[i] == 0x24 && i + 1 < bytes.len() && bytes[i + 1] == 0x7B {
      let end = i + 2;
      while end < bytes.len() && bytes[end] != 0x7D { end = end + 1; }
      if end >= bytes.len() {
        return Result::Err(IniError::InterpolationError(line_num, String::from(value.slice(i, bytes.len()))));
      }
      let ref_name = value.slice(i + 2, end);
      let dot = find_byte(ref_name, 0x2E);
      let lookup_section = current_section;
      let lookup_key = ref_name;
      if dot.is_some() {
        lookup_section = ref_name.slice(0, dot.unwrap());
        lookup_key = ref_name.slice(dot.unwrap() + 1, ref_name.len());
      }
      let found = doc.get(lookup_section, lookup_key);
      if found.is_none() {
        return Result::Err(IniError::InterpolationError(line_num, String::from(ref_name)));
      }
      out.push_str(found.unwrap());
      changed = true;
      i = end + 1;
    } else {
      out.push(bytes[i] as char);
      i = i + 1;
    }
  }
  return Result::Ok((changed, out));
}

/// Write a value: quote it if it contains characters that would otherwise
/// get parsed differently (separators, leading/trailing spaces, newlines,
/// or comment chars).
function write_value(buf: refmut<String>, v: ref<str>): void {
  if needs_quoting(v) {
    buf.push('"' as char);
    let bytes = v.as_bytes();
    let i: usize = 0;
    while i < bytes.len() {
      let b = bytes[i];
      if b == 0x22 { buf.push_str("\\\""); }
      else if b == 0x5C { buf.push_str("\\\\"); }
      else if b == 0x0A { buf.push_str("\\n"); }
      else if b == 0x0D { buf.push_str("\\r"); }
      else if b == 0x09 { buf.push_str("\\t"); }
      else { buf.push(b as char); }
      i = i + 1;
    }
    buf.push('"' as char);
  } else {
    buf.push_str(v);
  }
}

function needs_quoting(v: ref<str>): bool {
  let bytes = v.as_bytes();
  if bytes.len() == 0 { return false; }
  if is_ws(bytes[0]) || is_ws(bytes[bytes.len() - 1]) { return true; }
  let i: usize = 0;
  while i < bytes.len() {
    let b = bytes[i];
    if b == 0x0A || b == 0x0D || b == 0x22 || b == 0x5C { return true; }
    if b == 0x23 || b == 0x3B { return true; } // comment chars
    i = i + 1;
  }
  return false;
}
