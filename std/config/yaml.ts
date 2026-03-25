// std/config/yaml.ts — Basic YAML parser/serializer (RFC-0019, subset)
// Supports: scalars, sequences (- item), mappings (key: value), basic nesting via indentation.

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
  fn as_sequence(ref<Self>): Option<ref<Vec<YamlValue>>> {
    match self { YamlValue::Sequence(s) => Option::Some(s), _ => Option::None }
  }
  fn as_mapping(ref<Self>): Option<ref<Vec<(own<String>, YamlValue)>>> {
    match self { YamlValue::Mapping(m) => Option::Some(m), _ => Option::None }
  }

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

/// Parsed line representation.
struct YamlLine {
  indent: usize,
  content: own<String>,
  line_num: usize,
}

/// Parse a YAML string.
function parse(input: ref<str>): Result<own<YamlValue>, YamlError> {
  let lines: own<Vec<YamlLine>> = Vec::new();
  let raw_lines = input.split('\n');
  let i: usize = 0;
  while i < raw_lines.len() {
    let line = raw_lines.get(i).unwrap().as_str();
    let indent = count_indent(line);
    let trimmed = trim_start(line);
    // Skip empty lines and comments.
    if trimmed.len() > 0 && trimmed.as_bytes()[0] != 0x23 {
      lines.push(YamlLine {
        indent: indent,
        content: String::from(trimmed),
        line_num: i + 1,
      });
    }
    i = i + 1;
  }

  if lines.is_empty() { return Result::Ok(YamlValue::Null); }

  let pos: usize = 0;
  return parse_node(ref lines, refmut pos, 0);
}

function parse_node(lines: ref<Vec<YamlLine>>, pos: refmut<usize>, min_indent: usize): Result<own<YamlValue>, YamlError> {
  if *pos >= lines.len() { return Result::Ok(YamlValue::Null); }

  let line = lines.get(*pos).unwrap();
  if line.indent < min_indent { return Result::Ok(YamlValue::Null); }

  let content = line.content.as_str();

  // Check if it's a sequence item.
  if content.starts_with("- ") || content == "-" {
    return parse_sequence(lines, pos, line.indent);
  }

  // Check if it's a mapping.
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
    let key = String::from(content.slice(0, cp));
    let after = trim_start(content.slice(cp + 1, content.len()));
    *pos = *pos + 1;

    if after.len() > 0 {
      entries.push((key, parse_scalar(after)));
    } else {
      let val = parse_node(lines, pos, base_indent + 2)?;
      entries.push((key, val));
    }
  }
  return Result::Ok(YamlValue::Mapping(entries));
}

function parse_scalar(s: ref<str>): own<YamlValue> {
  if s == "null" || s == "~" { return YamlValue::Null; }
  if s == "true" { return YamlValue::Bool(true); }
  if s == "false" { return YamlValue::Bool(false); }

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

  // Strip quotes if present.
  if bytes.len() >= 2 {
    let first = bytes[0];
    let last = bytes[bytes.len() - 1];
    if (first == 0x22 && last == 0x22) || (first == 0x27 && last == 0x27) {
      return YamlValue::String(String::from(s.slice(1, bytes.len() - 1)));
    }
  }
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

function find_colon(s: ref<str>): Option<usize> {
  let bytes = s.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    if bytes[i] == 0x3A && (i + 1 >= bytes.len() || bytes[i + 1] == 0x20) {
      return Option::Some(i);
    }
    i = i + 1;
  }
  return Option::None;
}

/// Serialize a YamlValue to a YAML string.
function stringify(value: ref<YamlValue>): own<String> {
  let buf = String::new();
  write_yaml(refmut buf, value, 0, false);
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
