// std/config/yaml.ts — YAML 1.2 core schema parser/serializer (RFC-0019 §3.2)
// Supports:
//   - Block-style mappings (key: value) and sequences (- item) via indentation
//   - Flow-style mappings {a: 1, b: 2} and sequences [1, 2, 3]
//   - Plain, single-quoted, and double-quoted scalars (with escapes)
//   - Core schema type detection: null/~, true/false, int, float, string
//   - Multi-document streams separated by "---"
// Not supported (out of scope per task): anchors &, aliases *, explicit tags !!.

/// Represents a YAML value.
enum YamlValue {
  Null,
  Bool(bool),
  Int(i64),
  Float(f64),
  String(own<String>),
  Sequence(own<Vec<YamlValue>>),
  Mapping(own<Vec<(own<String>, YamlValue)>>),
}

/// Error type for YAML parsing.
enum YamlError {
  UnexpectedToken(usize),
  IndentationError(usize),
  UnexpectedEof,
  InvalidValue(usize),
}

impl Display for YamlError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      YamlError::UnexpectedToken(line) => f.write_str("unexpected token at line ").and_then(|_| line.fmt(f)),
      YamlError::IndentationError(line) => f.write_str("indentation error at line ").and_then(|_| line.fmt(f)),
      YamlError::UnexpectedEof => f.write_str("unexpected end of YAML input"),
      YamlError::InvalidValue(line) => f.write_str("invalid value at line ").and_then(|_| line.fmt(f)),
    }
  }
}

impl YamlValue {
  fn as_str(ref<Self>): Option<ref<str>> {
    match self { YamlValue::String(s) => Option::Some(s.as_str()), _ => Option::None }
  }
  fn as_i64(ref<Self>): Option<i64> {
    match self { YamlValue::Int(v) => Option::Some(*v), _ => Option::None }
  }
  fn as_f64(ref<Self>): Option<f64> {
    match self { YamlValue::Float(v) => Option::Some(*v), _ => Option::None }
  }
  fn as_bool(ref<Self>): Option<bool> {
    match self { YamlValue::Bool(v) => Option::Some(*v), _ => Option::None }
  }
  fn is_null(ref<Self>): bool {
    match self { YamlValue::Null => true, _ => false }
  }
  fn as_sequence(ref<Self>): Option<ref<Vec<YamlValue>>> {
    match self { YamlValue::Sequence(s) => Option::Some(s), _ => Option::None }
  }
  fn as_mapping(ref<Self>): Option<ref<Vec<(own<String>, YamlValue)>>> {
    match self { YamlValue::Mapping(m) => Option::Some(m), _ => Option::None }
  }

  /// Look up an entry by key in a mapping.
  fn get(ref<Self>, key: ref<str>): Option<ref<YamlValue>> {
    match self {
      YamlValue::Mapping(entries) => {
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

/// Parsed line representation used by the block-style parser.
struct YamlLine {
  indent: usize,
  content: own<String>,
  line_num: usize,
}

/// Parse a single YAML document. Additional documents in the stream
/// (after a `---` separator) are ignored.
function parse(input: ref<str>): Result<own<YamlValue>, YamlError> {
  let lines = collect_lines(input, true);
  return build_document(ref lines);
}

/// Parse a multi-document YAML stream into a vector of values.
/// Documents are separated by lines containing only `---`; a line of `...`
/// marks the explicit end of a document.
function parse_all(input: ref<str>): Result<own<Vec<YamlValue>>, YamlError> {
  let documents: own<Vec<YamlValue>> = Vec::new();
  let doc_lines: own<Vec<YamlLine>> = Vec::new();
  let raw_lines = input.split('\n');
  let i: usize = 0;
  while i < raw_lines.len() {
    let line = raw_lines.get(i).unwrap().as_str();
    let trimmed = trim_end(trim_start(line));
    if trimmed == "---" || trimmed == "..." {
      if !doc_lines.is_empty() {
        documents.push(build_document(ref doc_lines)?);
        doc_lines.clear();
      }
      i = i + 1;
      continue;
    }
    push_content_line(refmut doc_lines, line, i + 1);
    i = i + 1;
  }
  if !doc_lines.is_empty() {
    documents.push(build_document(ref doc_lines)?);
  }
  return Result::Ok(documents);
}

/// Scan input and build the list of content-bearing lines for a single
/// document. If `stop_at_separator` is true, stop at the first `---`/`...`.
function collect_lines(input: ref<str>, stop_at_separator: bool): own<Vec<YamlLine>> {
  let lines: own<Vec<YamlLine>> = Vec::new();
  let raw_lines = input.split('\n');
  let i: usize = 0;
  while i < raw_lines.len() {
    let line = raw_lines.get(i).unwrap().as_str();
    let trimmed = trim_end(trim_start(line));
    if stop_at_separator && (trimmed == "---" || trimmed == "...") {
      if !lines.is_empty() { break; }
      i = i + 1;
      continue;
    }
    push_content_line(refmut lines, line, i + 1);
    i = i + 1;
  }
  return lines;
}

/// Append `line` to `out` unless it is blank or a whole-line comment.
function push_content_line(out: refmut<Vec<YamlLine>>, line: ref<str>, line_num: usize): void {
  let indent = count_indent(line);
  let trimmed = trim_end(trim_start(line));
  if trimmed.len() == 0 || trimmed.as_bytes()[0] == 0x23 { return; }
  out.push(YamlLine {
    indent: indent,
    content: String::from(strip_trailing_comment(trimmed)),
    line_num: line_num,
  });
}

/// Build a YamlValue from the accumulated lines of one document.
function build_document(lines: ref<Vec<YamlLine>>): Result<own<YamlValue>, YamlError> {
  if lines.is_empty() { return Result::Ok(YamlValue::Null); }
  let pos: usize = 0;
  return parse_node(lines, refmut pos, 0);
}

function parse_node(lines: ref<Vec<YamlLine>>, pos: refmut<usize>, min_indent: usize): Result<own<YamlValue>, YamlError> {
  if *pos >= lines.len() { return Result::Ok(YamlValue::Null); }

  let line = lines.get(*pos).unwrap();
  if line.indent < min_indent { return Result::Ok(YamlValue::Null); }

  let content = line.content.as_str();

  // Flow-style entry on a single line: parse as flow.
  if content.len() > 0 {
    let first = content.as_bytes()[0];
    if first == 0x5B || first == 0x7B { // '[' or '{'
      *pos = *pos + 1;
      return parse_flow(content);
    }
  }

  // Block-style sequence.
  if content.starts_with("- ") || content == "-" {
    return parse_sequence(lines, pos, line.indent);
  }

  // Block-style mapping.
  let colon_pos = find_colon(content);
  if colon_pos.is_some() {
    return parse_mapping(lines, pos, line.indent);
  }

  // Scalar value.
  *pos = *pos + 1;
  return Result::Ok(parse_scalar(content));
}

function parse_sequence(lines: ref<Vec<YamlLine>>, pos: refmut<usize>, base_indent: usize): Result<own<YamlValue>, YamlError> {
  let items: own<Vec<YamlValue>> = Vec::new();
  while *pos < lines.len() {
    let line = lines.get(*pos).unwrap();
    if line.indent != base_indent { break; }
    let content = line.content.as_str();
    if !content.starts_with("- ") && content != "-" { break; }

    if content == "-" {
      *pos = *pos + 1;
      let val = parse_node(lines, pos, base_indent + 2)?;
      items.push(val);
    } else {
      let val_str = content.slice(2, content.len());
      let vb = val_str.as_bytes();
      // Flow-style element inline with "- ".
      if vb.len() > 0 && (vb[0] == 0x5B || vb[0] == 0x7B) {
        *pos = *pos + 1;
        items.push(parse_flow(val_str)?);
        continue;
      }
      let colon = find_colon(val_str);
      if colon.is_some() {
        *pos = *pos + 1;
        let inner_lines: own<Vec<YamlLine>> = Vec::new();
        inner_lines.push(YamlLine { indent: 0, content: String::from(val_str), line_num: line.line_num });
        while *pos < lines.len() {
          let next = lines.get(*pos).unwrap();
          if next.indent <= base_indent { break; }
          inner_lines.push(YamlLine { indent: next.indent - base_indent - 2, content: next.content.clone(), line_num: next.line_num });
          *pos = *pos + 1;
        }
        let inner_pos: usize = 0;
        let val = parse_mapping(ref inner_lines, refmut inner_pos, 0)?;
        items.push(val);
      } else {
        *pos = *pos + 1;
        items.push(parse_scalar(val_str));
      }
    }
  }
  return Result::Ok(YamlValue::Sequence(items));
}

function parse_mapping(lines: ref<Vec<YamlLine>>, pos: refmut<usize>, base_indent: usize): Result<own<YamlValue>, YamlError> {
  let entries: own<Vec<(own<String>, YamlValue)>> = Vec::new();
  while *pos < lines.len() {
    let line = lines.get(*pos).unwrap();
    if line.indent != base_indent { break; }
    let content = line.content.as_str();
    let colon = find_colon(content);
    if colon.is_none() { break; }
    let cp = colon.unwrap();
    let key = unquote(content.slice(0, cp));
    let after = trim_start(content.slice(cp + 1, content.len()));
    *pos = *pos + 1;

    if after.len() > 0 {
      let ab = after.as_bytes();
      if ab[0] == 0x5B || ab[0] == 0x7B {
        entries.push((key, parse_flow(after)?));
      } else {
        entries.push((key, parse_scalar(after)));
      }
    } else {
      let val = parse_node(lines, pos, base_indent + 2)?;
      entries.push((key, val));
    }
  }
  return Result::Ok(YamlValue::Mapping(entries));
}

/// Parse a YAML flow-style value (sequence `[...]` or mapping `{...}`).
function parse_flow(s: ref<str>): Result<own<YamlValue>, YamlError> {
  let pos: usize = 0;
  let v = parse_flow_value(s, refmut pos)?;
  return Result::Ok(v);
}

function parse_flow_value(s: ref<str>, pos: refmut<usize>): Result<own<YamlValue>, YamlError> {
  skip_flow_ws(s, pos);
  if *pos >= s.len() { return Result::Err(YamlError::UnexpectedEof); }
  let c = s.as_bytes()[*pos];
  if c == 0x5B { return parse_flow_sequence(s, pos); }
  if c == 0x7B { return parse_flow_mapping(s, pos); }
  // Plain scalar: read until , ] } end.
  let start = *pos;
  if c == 0x22 || c == 0x27 {
    let quoted = read_quoted(s, pos)?;
    return Result::Ok(parse_scalar(quoted.as_str()));
  }
  while *pos < s.len() {
    let ch = s.as_bytes()[*pos];
    if ch == 0x2C || ch == 0x5D || ch == 0x7D { break; }
    *pos = *pos + 1;
  }
  let raw = trim_end(s.slice(start, *pos));
  return Result::Ok(parse_scalar(raw));
}

function parse_flow_sequence(s: ref<str>, pos: refmut<usize>): Result<own<YamlValue>, YamlError> {
  *pos = *pos + 1; // consume '['
  let items: own<Vec<YamlValue>> = Vec::new();
  skip_flow_ws(s, pos);
  if *pos < s.len() && s.as_bytes()[*pos] == 0x5D {
    *pos = *pos + 1;
    return Result::Ok(YamlValue::Sequence(items));
  }
  loop {
    skip_flow_ws(s, pos);
    let v = parse_flow_value(s, pos)?;
    items.push(v);
    skip_flow_ws(s, pos);
    if *pos >= s.len() { return Result::Err(YamlError::UnexpectedEof); }
    let ch = s.as_bytes()[*pos];
    if ch == 0x2C { *pos = *pos + 1; continue; }
    if ch == 0x5D { *pos = *pos + 1; return Result::Ok(YamlValue::Sequence(items)); }
    return Result::Err(YamlError::UnexpectedToken(0));
  }
}

function parse_flow_mapping(s: ref<str>, pos: refmut<usize>): Result<own<YamlValue>, YamlError> {
  *pos = *pos + 1; // consume '{'
  let entries: own<Vec<(own<String>, YamlValue)>> = Vec::new();
  skip_flow_ws(s, pos);
  if *pos < s.len() && s.as_bytes()[*pos] == 0x7D {
    *pos = *pos + 1;
    return Result::Ok(YamlValue::Mapping(entries));
  }
  loop {
    skip_flow_ws(s, pos);
    // Read key: either quoted (returned already unescaped) or plain (ends at ':').
    let key: own<String>;
    if *pos < s.len() && (s.as_bytes()[*pos] == 0x22 || s.as_bytes()[*pos] == 0x27) {
      key = read_quoted(s, pos)?;
    } else {
      let start = *pos;
      while *pos < s.len() {
        let ch = s.as_bytes()[*pos];
        if ch == 0x3A || ch == 0x2C || ch == 0x7D { break; }
        *pos = *pos + 1;
      }
      key = String::from(trim_end(s.slice(start, *pos)));
    }
    skip_flow_ws(s, pos);
    if *pos >= s.len() || s.as_bytes()[*pos] != 0x3A {
      return Result::Err(YamlError::UnexpectedToken(0));
    }
    *pos = *pos + 1; // consume ':'
    skip_flow_ws(s, pos);
    let v = parse_flow_value(s, pos)?;
    entries.push((key, v));
    skip_flow_ws(s, pos);
    if *pos >= s.len() { return Result::Err(YamlError::UnexpectedEof); }
    let ch = s.as_bytes()[*pos];
    if ch == 0x2C { *pos = *pos + 1; continue; }
    if ch == 0x7D { *pos = *pos + 1; return Result::Ok(YamlValue::Mapping(entries)); }
    return Result::Err(YamlError::UnexpectedToken(0));
  }
}

function skip_flow_ws(s: ref<str>, pos: refmut<usize>): void {
  while *pos < s.len() {
    let c = s.as_bytes()[*pos];
    if c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D { *pos = *pos + 1; }
    else { break; }
  }
}

/// Read a quoted string beginning at `pos` and advance past the closing quote.
/// Handles the standard escape sequences for double-quoted strings and literal
/// content for single-quoted strings.
function read_quoted(s: ref<str>, pos: refmut<usize>): Result<own<String>, YamlError> {
  let bytes = s.as_bytes();
  let quote = bytes[*pos];
  *pos = *pos + 1;
  let out = String::new();
  if quote == 0x27 {
    // Single-quoted: literal content, '' is an escaped single-quote.
    while *pos < s.len() {
      let c = bytes[*pos];
      if c == 0x27 {
        if *pos + 1 < s.len() && bytes[*pos + 1] == 0x27 {
          out.push('\'' as char);
          *pos = *pos + 2;
          continue;
        }
        *pos = *pos + 1;
        return Result::Ok(out);
      }
      out.push(c as char);
      *pos = *pos + 1;
    }
    return Result::Err(YamlError::UnexpectedEof);
  }
  // Double-quoted: handle escapes.
  while *pos < s.len() {
    let c = bytes[*pos];
    if c == 0x22 {
      *pos = *pos + 1;
      return Result::Ok(out);
    }
    if c == 0x5C {
      if *pos + 1 >= s.len() { return Result::Err(YamlError::UnexpectedEof); }
      let esc = bytes[*pos + 1];
      if esc == 0x6E { out.push(0x0A as char); }       // \n
      else if esc == 0x74 { out.push(0x09 as char); }  // \t
      else if esc == 0x72 { out.push(0x0D as char); }  // \r
      else if esc == 0x30 { out.push(0x00 as char); }  // \0
      else if esc == 0x5C { out.push('\\' as char); }  // \\
      else if esc == 0x22 { out.push('"' as char); }   // \"
      else if esc == 0x27 { out.push('\'' as char); }  // \'
      else if esc == 0x2F { out.push('/' as char); }   // \/
      else { out.push(esc as char); }
      *pos = *pos + 2;
      continue;
    }
    out.push(c as char);
    *pos = *pos + 1;
  }
  return Result::Err(YamlError::UnexpectedEof);
}

/// If `s` is wrapped in matching quotes, return a String with the escaped
/// content. Otherwise return the input as a String unchanged.
function unquote(s: ref<str>): own<String> {
  let bytes = s.as_bytes();
  if bytes.len() >= 2 {
    let first = bytes[0];
    let last = bytes[bytes.len() - 1];
    if (first == 0x22 && last == 0x22) || (first == 0x27 && last == 0x27) {
      let pos: usize = 0;
      let unquoted = read_quoted(s, refmut pos);
      match unquoted {
        Result::Ok(st) => return st,
        Result::Err(_) => return String::from(s.slice(1, bytes.len() - 1)),
      }
    }
  }
  return String::from(s);
}

function parse_scalar(s: ref<str>): own<YamlValue> {
  // Handle quoted strings: they are always strings, never re-interpreted.
  if s.len() >= 2 {
    let first = s.as_bytes()[0];
    let last = s.as_bytes()[s.len() - 1];
    if (first == 0x22 && last == 0x22) || (first == 0x27 && last == 0x27) {
      return YamlValue::String(unquote(s));
    }
  }
  if s == "null" || s == "~" || s == "Null" || s == "NULL" || s.len() == 0 {
    return YamlValue::Null;
  }
  if s == "true" || s == "True" || s == "TRUE" { return YamlValue::Bool(true); }
  if s == "false" || s == "False" || s == "FALSE" { return YamlValue::Bool(false); }

  let bytes = s.as_bytes();
  let start: usize = 0;
  if bytes.len() > 0 && (bytes[0] == 0x2D || bytes[0] == 0x2B) { start = 1; }

  // Try integer.
  let is_int = start < bytes.len();
  let j = start;
  while j < bytes.len() {
    if bytes[j] < 0x30 || bytes[j] > 0x39 { is_int = false; break; }
    j = j + 1;
  }
  if is_int && bytes.len() > 0 { return YamlValue::Int(parse_i64(s)); }

  // Try float.
  let has_dot = false;
  j = start;
  let is_float = start < bytes.len();
  while j < bytes.len() {
    if bytes[j] == 0x2E { has_dot = true; }
    else if bytes[j] == 0x65 || bytes[j] == 0x45 { has_dot = true; }
    else if (bytes[j] < 0x30 || bytes[j] > 0x39) && bytes[j] != 0x2B && bytes[j] != 0x2D { is_float = false; break; }
    j = j + 1;
  }
  if is_float && has_dot { return YamlValue::Float(parse_f64(s)); }

  return YamlValue::String(String::from(s));
}

function count_indent(s: ref<str>): usize {
  let bytes = s.as_bytes();
  let i: usize = 0;
  while i < bytes.len() && bytes[i] == 0x20 { i = i + 1; }
  return i;
}

function trim_start(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let i: usize = 0;
  while i < bytes.len() && (bytes[i] == 0x20 || bytes[i] == 0x09) { i = i + 1; }
  return s.slice(i, bytes.len());
}

function trim_end(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let n = bytes.len();
  while n > 0 {
    let c = bytes[n - 1];
    if c == 0x20 || c == 0x09 || c == 0x0D { n = n - 1; } else { break; }
  }
  return s.slice(0, n);
}

/// Strip an unquoted trailing `# comment` from a line.
function strip_trailing_comment(s: ref<str>): ref<str> {
  let bytes = s.as_bytes();
  let i: usize = 0;
  let in_single = false;
  let in_double = false;
  while i < bytes.len() {
    let c = bytes[i];
    if !in_single && !in_double && c == 0x23 {
      // `#` must be preceded by whitespace (or start of string) to be a comment.
      if i == 0 || bytes[i - 1] == 0x20 || bytes[i - 1] == 0x09 {
        return trim_end(s.slice(0, i));
      }
    }
    if c == 0x22 && !in_single { in_double = !in_double; }
    else if c == 0x27 && !in_double { in_single = !in_single; }
    else if c == 0x5C && in_double { i = i + 1; } // skip escaped byte
    i = i + 1;
  }
  return s;
}

/// Find the first `:` that marks a key/value separator (followed by space,
/// end-of-line, or nothing). Ignores colons inside quoted segments.
function find_colon(s: ref<str>): Option<usize> {
  let bytes = s.as_bytes();
  let i: usize = 0;
  let in_single = false;
  let in_double = false;
  while i < bytes.len() {
    let c = bytes[i];
    if c == 0x22 && !in_single { in_double = !in_double; }
    else if c == 0x27 && !in_double { in_single = !in_single; }
    else if !in_single && !in_double && c == 0x3A {
      if i + 1 >= bytes.len() || bytes[i + 1] == 0x20 || bytes[i + 1] == 0x09 {
        return Option::Some(i);
      }
    }
    i = i + 1;
  }
  return Option::None;
}

/// Serialize a YamlValue to a YAML string (RFC-0019 API name).
function to_string(value: ref<YamlValue>): own<String> {
  return stringify(value);
}

/// Serialize a YamlValue to a YAML string.
function stringify(value: ref<YamlValue>): own<String> {
  let buf = String::new();
  write_yaml(refmut buf, value, 0, false);
  return buf;
}

/// Serialize multiple documents separated by `---` markers.
function to_string_all(values: ref<Vec<YamlValue>>): own<String> {
  let buf = String::new();
  let i: usize = 0;
  while i < values.len() {
    if i > 0 { buf.push_str("---\n"); }
    write_yaml(refmut buf, values.get(i).unwrap(), 0, false);
    i = i + 1;
  }
  return buf;
}

function write_yaml(buf: refmut<String>, value: ref<YamlValue>, indent: usize, inline: bool): void {
  match value {
    YamlValue::Null => buf.push_str("null"),
    YamlValue::Bool(v) => {
      if *v { buf.push_str("true"); } else { buf.push_str("false"); }
    },
    YamlValue::Int(v) => buf.push_str(i64_to_string(*v).as_str()),
    YamlValue::Float(v) => buf.push_str(f64_to_string(*v).as_str()),
    YamlValue::String(s) => buf.push_str(s.as_str()),
    YamlValue::Sequence(items) => {
      if items.is_empty() { buf.push_str("[]"); return; }
      let i: usize = 0;
      while i < items.len() {
        if i > 0 || !inline { write_indent(buf, indent); }
        buf.push_str("- ");
        write_yaml(buf, items.get(i).unwrap(), indent + 2, true);
        buf.push(0x0A as char);
        i = i + 1;
      }
    },
    YamlValue::Mapping(entries) => {
      if entries.is_empty() { buf.push_str("{}"); return; }
      let i: usize = 0;
      while i < entries.len() {
        if i > 0 || !inline { write_indent(buf, indent); }
        let entry = entries.get(i).unwrap();
        buf.push_str(entry.0.as_str());
        buf.push_str(": ");
        match entry.1 {
          YamlValue::Mapping(_) | YamlValue::Sequence(_) => {
            buf.push(0x0A as char);
            write_yaml(buf, ref entry.1, indent + 2, false);
          },
          _ => {
            write_yaml(buf, ref entry.1, indent, true);
            buf.push(0x0A as char);
          },
        }
        i = i + 1;
      }
    },
  }
}

function write_indent(buf: refmut<String>, indent: usize): void {
  let i: usize = 0;
  while i < indent { buf.push(' ' as char); i = i + 1; }
}
