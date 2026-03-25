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
