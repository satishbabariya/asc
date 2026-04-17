// std/json/serializer.ts — JSON serialization (RFC-0016)

import { JsonValue } from './value';

/// Serialize a JsonValue to a compact JSON string.
function stringify(value: ref<JsonValue>): own<String> {
  let buf = String::new();
  write_value(ref buf, value, 0, 0);
  return buf;
}

/// Serialize a JsonValue to a pretty-printed JSON string.
function stringify_pretty(value: ref<JsonValue>, indent: usize): own<String> {
  let buf = String::new();
  write_value(ref buf, value, indent, 0);
  return buf;
}

/// Internal: write a value into the buffer.
function write_value(buf: refmut<String>, value: ref<JsonValue>, indent: usize, depth: usize): void {
  match value {
    JsonValue::Null => buf.push_str("null"),
    JsonValue::Bool(v) => {
      if *v { buf.push_str("true"); }
      else { buf.push_str("false"); }
    },
    JsonValue::Int(v) => {
      let s = i64_to_string(*v);
      buf.push_str(s.as_str());
    },
    JsonValue::Uint(v) => {
      let s = u64_to_string(*v);
      buf.push_str(s.as_str());
    },
    JsonValue::Float(v) => {
      let s = f64_to_string(*v);
      buf.push_str(s.as_str());
    },
    JsonValue::Str(s) => {
      write_escaped_string(buf, s.as_str());
    },
    JsonValue::Array(arr) => {
      write_array(buf, arr, indent, depth);
    },
    JsonValue::Object(entries) => {
      write_object(buf, entries, indent, depth);
    },
  }
}

/// Internal: write a JSON-escaped string with surrounding quotes.
function write_escaped_string(buf: refmut<String>, s: ref<str>): void {
  buf.push('"' as char);
  let bytes = s.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    let c = bytes[i];
    if c == 0x22 { buf.push_str("\\\""); }        // "
    else if c == 0x5C { buf.push_str("\\\\"); }    // backslash
    else if c == 0x08 { buf.push_str("\\b"); }     // backspace
    else if c == 0x0C { buf.push_str("\\f"); }     // form feed
    else if c == 0x0A { buf.push_str("\\n"); }     // newline
    else if c == 0x0D { buf.push_str("\\r"); }     // carriage return
    else if c == 0x09 { buf.push_str("\\t"); }     // tab
    else if c < 0x20 {
      // Control characters: \u00XX
      buf.push_str("\\u00");
      buf.push(hex_digit(c >> 4) as char);
      buf.push(hex_digit(c & 0x0F) as char);
    } else {
      buf.push(c as char);
    }
    i = i + 1;
  }
  buf.push('"' as char);
}

function hex_digit(v: u8): u8 {
  if v < 10 { return 0x30 + v; }
  return 0x61 + v - 10;
}

function write_indent(buf: refmut<String>, indent: usize, depth: usize): void {
  if indent == 0 { return; }
  buf.push(0x0A as char); // newline
  let total = indent * depth;
  let i: usize = 0;
  while i < total {
    buf.push(' ' as char);
    i = i + 1;
  }
}

function write_array(buf: refmut<String>, arr: ref<Vec<JsonValue>>, indent: usize, depth: usize): void {
  buf.push('[' as char);
  if arr.is_empty() {
    buf.push(']' as char);
    return;
  }
  let i: usize = 0;
  while i < arr.len() {
    if i > 0 { buf.push(',' as char); }
    if indent > 0 {
      write_indent(buf, indent, depth + 1);
    }
    write_value(buf, arr.get(i).unwrap(), indent, depth + 1);
    i = i + 1;
  }
  if indent > 0 {
    write_indent(buf, indent, depth);
  }
  buf.push(']' as char);
}

function write_object(
  buf: refmut<String>,
  entries: ref<Vec<(own<String>, JsonValue)>>,
  indent: usize,
  depth: usize,
): void {
  buf.push('{' as char);
  if entries.is_empty() {
    buf.push('}' as char);
    return;
  }
  let i: usize = 0;
  while i < entries.len() {
    if i > 0 { buf.push(',' as char); }
    if indent > 0 {
      write_indent(buf, indent, depth + 1);
    }
    let entry = entries.get(i).unwrap();
    write_escaped_string(buf, entry.0.as_str());
    buf.push(':' as char);
    if indent > 0 { buf.push(' ' as char); }
    write_value(buf, ref entry.1, indent, depth + 1);
    i = i + 1;
  }
  if indent > 0 {
    write_indent(buf, indent, depth);
  }
  buf.push('}' as char);
}

// -----------------------------------------------------------------------------
// JsonWriter — streaming serializer (RFC-0016 §4)
// -----------------------------------------------------------------------------

/// Frame tags for the JsonWriter state stack.
/// - `InObject`: we are inside an object, expecting either a key or `}`.
/// - `InObjectValue`: a key has been written; the next call must write a value.
/// - `InArray`: we are inside an array, expecting either a value or `]`.
const WRITER_FRAME_OBJECT: u8 = 1;
const WRITER_FRAME_OBJECT_VALUE: u8 = 2;
const WRITER_FRAME_ARRAY: u8 = 3;

/// Streaming JSON serializer with automatic comma/colon insertion.
///
/// Maintains an internal stack to track nesting. In debug mode (the default),
/// invalid sequences — such as writing a value before the containing object has
/// received a key, or calling `end_array` inside an object — trigger a panic.
/// Release builds (`debug_checks == false`) skip validation for maximum speed.
///
/// Output is written directly to an owned `String` buffer; call `finish()` to
/// consume the writer and retrieve the serialized document.
struct JsonWriter {
  buf: own<String>,
  /// One entry per open container. Values are `WRITER_FRAME_*` constants.
  stack: own<Vec<u8>>,
  /// Number of values or key/value pairs written into the current container.
  /// Used to decide whether a preceding comma is required.
  count: own<Vec<usize>>,
  /// Pretty-print indent width (spaces per level). `0` means compact.
  indent: usize,
  /// Whether runtime state-machine validation is active.
  debug_checks: bool,
}

impl JsonWriter {
  /// Create a new compact writer backed by a fresh String buffer.
  fn new(): own<JsonWriter> {
    return JsonWriter {
      buf: String::new(),
      stack: Vec::new(),
      count: Vec::new(),
      indent: 0,
      debug_checks: true,
    };
  }

  /// Create a pretty-printing writer. `indent` is the number of spaces per
  /// nesting level (e.g. 2 or 4). An `indent` of 0 is equivalent to `new()`.
  fn pretty(indent: usize): own<JsonWriter> {
    return JsonWriter {
      buf: String::new(),
      stack: Vec::new(),
      count: Vec::new(),
      indent: indent,
      debug_checks: true,
    };
  }

  /// Disable runtime validation for performance. Misuse becomes undefined
  /// behaviour, exactly matching the release-mode contract in the RFC.
  fn set_debug_checks(refmut<Self>, enabled: bool): void {
    self.debug_checks = enabled;
  }

  /// Consume the writer and return the serialized JSON document. In debug mode
  /// this panics if the document is incomplete (unclosed container).
  fn finish(own<Self>): own<String> {
    if self.debug_checks && !self.stack.is_empty() {
      panic("JsonWriter::finish called with unclosed container");
    }
    return self.buf;
  }

  // --- structural ---------------------------------------------------------

  fn begin_object(refmut<Self>): void {
    self.before_value();
    self.buf.push('{' as char);
    self.stack.push(WRITER_FRAME_OBJECT);
    self.count.push(0);
  }

  fn end_object(refmut<Self>): void {
    if self.debug_checks {
      if self.stack.is_empty() {
        panic("JsonWriter::end_object called with no open container");
      }
      let top = *self.stack.get(self.stack.len() - 1).unwrap();
      if top != WRITER_FRAME_OBJECT {
        panic("JsonWriter::end_object called while not inside an object");
      }
    }
    let n = *self.count.get(self.count.len() - 1).unwrap();
    self.stack.pop();
    self.count.pop();
    if self.indent > 0 && n > 0 {
      write_indent(ref self.buf, self.indent, self.stack.len());
    }
    self.buf.push('}' as char);
    self.bump_count();
  }

  fn begin_array(refmut<Self>): void {
    self.before_value();
    self.buf.push('[' as char);
    self.stack.push(WRITER_FRAME_ARRAY);
    self.count.push(0);
  }

  fn end_array(refmut<Self>): void {
    if self.debug_checks {
      if self.stack.is_empty() {
        panic("JsonWriter::end_array called with no open container");
      }
      let top = *self.stack.get(self.stack.len() - 1).unwrap();
      if top != WRITER_FRAME_ARRAY {
        panic("JsonWriter::end_array called while not inside an array");
      }
    }
    let n = *self.count.get(self.count.len() - 1).unwrap();
    self.stack.pop();
    self.count.pop();
    if self.indent > 0 && n > 0 {
      write_indent(ref self.buf, self.indent, self.stack.len());
    }
    self.buf.push(']' as char);
    self.bump_count();
  }

  fn write_key(refmut<Self>, key: ref<str>): void {
    if self.debug_checks {
      if self.stack.is_empty() {
        panic("JsonWriter::write_key called at document root");
      }
      let top = *self.stack.get(self.stack.len() - 1).unwrap();
      if top != WRITER_FRAME_OBJECT {
        panic("JsonWriter::write_key called while not inside an object");
      }
    }
    let idx = self.count.len() - 1;
    let n = *self.count.get(idx).unwrap();
    if n > 0 { self.buf.push(',' as char); }
    if self.indent > 0 {
      write_indent(ref self.buf, self.indent, self.stack.len());
    }
    write_escaped_string(ref self.buf, key);
    self.buf.push(':' as char);
    if self.indent > 0 { self.buf.push(' ' as char); }
    // Flip frame tag to "awaiting value". The frame will be restored to
    // WRITER_FRAME_OBJECT after the paired value is written.
    self.stack.set(self.stack.len() - 1, WRITER_FRAME_OBJECT_VALUE);
  }

  // --- primitives ---------------------------------------------------------

  fn write_null(refmut<Self>): void {
    self.before_value();
    self.buf.push_str("null");
    self.bump_count();
  }

  fn write_bool(refmut<Self>, v: bool): void {
    self.before_value();
    if v { self.buf.push_str("true"); }
    else { self.buf.push_str("false"); }
    self.bump_count();
  }

  fn write_i64(refmut<Self>, v: i64): void {
    self.before_value();
    let s = i64_to_string(v);
    self.buf.push_str(s.as_str());
    self.bump_count();
  }

  fn write_u64(refmut<Self>, v: u64): void {
    self.before_value();
    let s = u64_to_string(v);
    self.buf.push_str(s.as_str());
    self.bump_count();
  }

  fn write_f64(refmut<Self>, v: f64): void {
    self.before_value();
    let s = f64_to_string(v);
    self.buf.push_str(s.as_str());
    self.bump_count();
  }

  fn write_string(refmut<Self>, v: ref<str>): void {
    self.before_value();
    write_escaped_string(ref self.buf, v);
    self.bump_count();
  }

  // --- internal helpers ---------------------------------------------------

  /// Emit formatting that must appear before any value: a leading comma when
  /// the current container already has a sibling, then an indent for pretty
  /// mode. In debug mode this also rejects writing a value where a key is
  /// expected.
  fn before_value(refmut<Self>): void {
    if self.stack.is_empty() {
      // Top-level value. Allow exactly one root document write.
      if self.debug_checks && !self.buf.is_empty() {
        panic("JsonWriter: multiple top-level values");
      }
      return;
    }
    let idx = self.stack.len() - 1;
    let top = *self.stack.get(idx).unwrap();
    if self.debug_checks && top == WRITER_FRAME_OBJECT {
      panic("JsonWriter: value written where key was expected");
    }
    if top == WRITER_FRAME_ARRAY {
      let n = *self.count.get(idx).unwrap();
      if n > 0 { self.buf.push(',' as char); }
      if self.indent > 0 {
        write_indent(ref self.buf, self.indent, self.stack.len());
      }
    }
    // For WRITER_FRAME_OBJECT_VALUE the comma/indent were already written by
    // write_key — nothing to do here.
  }

  /// After a value is written, increment the current container's child count
  /// and, if we just wrote an object value, restore the frame tag so the next
  /// write must be a key.
  fn bump_count(refmut<Self>): void {
    if self.stack.is_empty() { return; }
    let idx = self.stack.len() - 1;
    let n = *self.count.get(idx).unwrap();
    self.count.set(idx, n + 1);
    let top = *self.stack.get(idx).unwrap();
    if top == WRITER_FRAME_OBJECT_VALUE {
      self.stack.set(idx, WRITER_FRAME_OBJECT);
    }
  }
}
