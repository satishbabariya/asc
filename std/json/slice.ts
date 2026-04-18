// std/json/slice.ts — JsonSlice zero-copy view (RFC-0016)

import { JsonValue, JsonError } from './value';

/// A zero-copy view into a JSON byte buffer.
/// Stores the raw bytes and lazily parses on access.
struct JsonSlice {
  data: ref<[u8]>,
  start: usize,
  end: usize,
}

/// The kind of JSON token found at a position.
enum JsonTokenKind {
  Null,
  Bool,
  Number,
  String,
  Array,
  Object,
}

/// Iterator yielding (key_slice, value_slice) pairs for JSON objects.
struct JsonObjectIter {
  data: ref<[u8]>,
  pos: usize,
  end: usize,
}

/// Iterator yielding value_slice entries for JSON arrays.
struct JsonArrayIter {
  data: ref<[u8]>,
  pos: usize,
  end: usize,
}

impl JsonSlice {
  /// Create a slice over the full input.
  fn new(data: ref<[u8]>): JsonSlice {
    return JsonSlice { data: data, start: 0, end: data.len() };
  }

  /// Create a slice over a sub-range.
  fn from_range(data: ref<[u8]>, start: usize, end: usize): JsonSlice {
    return JsonSlice { data: data, start: start, end: end };
  }

  /// Return the raw bytes of this slice.
  fn as_bytes(ref<Self>): ref<[u8]> {
    return self.data.slice(self.start, self.end);
  }

  /// Determine the kind of JSON value at the current position.
  fn token_kind(ref<Self>): Result<JsonTokenKind, JsonError> {
    let pos = self.skip_ws(self.start);
    if pos >= self.end { return Result::Err(JsonError::UnexpectedEof); }
    let c = self.data[pos];
    if c == 0x22 { return Result::Ok(JsonTokenKind::String); }
    if c == 0x7B { return Result::Ok(JsonTokenKind::Object); }
    if c == 0x5B { return Result::Ok(JsonTokenKind::Array); }
    if c == 0x74 || c == 0x66 { return Result::Ok(JsonTokenKind::Bool); }
    if c == 0x6E { return Result::Ok(JsonTokenKind::Null); }
    if c == 0x2D || (c >= 0x30 && c <= 0x39) { return Result::Ok(JsonTokenKind::Number); }
    return Result::Err(JsonError::UnexpectedToken(pos));
  }

  // --- Type-check predicates (RFC-0016 §2.2) ---
  // Each inspects only the first non-whitespace byte. Empty/invalid slices
  // report `false` for every kind.

  fn is_null(ref<Self>): bool {
    return self.peek_tag() == Option::Some(0x6E);
  }

  fn is_bool(ref<Self>): bool {
    let c = self.peek_tag();
    return c == Option::Some(0x74) || c == Option::Some(0x66);
  }

  fn is_number(ref<Self>): bool {
    let t = self.peek_tag();
    if t.is_none() { return false; }
    let c = t.unwrap();
    return c == 0x2D || (c >= 0x30 && c <= 0x39);
  }

  fn is_string(ref<Self>): bool {
    return self.peek_tag() == Option::Some(0x22);
  }

  fn is_array(ref<Self>): bool {
    return self.peek_tag() == Option::Some(0x5B);
  }

  fn is_object(ref<Self>): bool {
    return self.peek_tag() == Option::Some(0x7B);
  }

  // --- Typed accessors (RFC-0016 §2.2) ---

  fn as_bool(ref<Self>): Option<bool> {
    let t = self.peek_tag();
    if t == Option::Some(0x74) { return Option::Some(true); }
    if t == Option::Some(0x66) { return Option::Some(false); }
    return Option::None;
  }

  /// Parses the slice as an i64. Returns None for non-integers and fractional/exponent forms.
  fn as_i64(ref<Self>): Option<i64> {
    if !self.is_number() { return Option::None; }
    let p = self.skip_ws(self.start);
    let neg = self.data[p] == 0x2D;
    if neg { p = p + 1; }
    if p >= self.end { return Option::None; }

    let val: i64 = 0;
    while p < self.end {
      let d = self.data[p];
      if d < 0x30 || d > 0x39 { break; }
      val = val * 10 + (d - 0x30) as i64;
      p = p + 1;
    }
    if p < self.end {
      let t = self.data[p];
      if t == 0x2E || t == 0x65 || t == 0x45 { return Option::None; }
    }
    if neg { val = -val; }
    return Option::Some(val);
  }

  /// Parses the slice as an f64. Accepts integers and fractional/exponent forms.
  fn as_f64(ref<Self>): Option<f64> {
    if !self.is_number() { return Option::None; }
    let pos = self.skip_ws(self.start);
    let end = self.skip_value(pos);
    if end.is_err() { return Option::None; }
    let text = JsonSlice::from_range(self.data, pos, end.unwrap()).as_raw_str();
    return Option::Some(parse_f64(text));
  }

  /// Zero-copy string extraction when the raw JSON string has no escape sequences.
  /// Returns None if the value is not a string or contains escapes (use `as_str_owned`).
  fn as_str(ref<Self>): Option<ref<str>> {
    let raw = self.get_raw_str();
    if raw.is_err() { return Option::None; }
    let r = raw.unwrap();
    let i = r.start;
    while i < r.end {
      if r.data[i] == 0x5C { return Option::None; }
      i = i + 1;
    }
    return Option::Some(r.as_raw_str());
  }

  /// Always-succeeding string extraction. Allocates if escape sequences are present.
  fn as_str_owned(ref<Self>): Option<own<String>> {
    if !self.is_string() { return Option::None; }
    let parsed = self.parse();
    if parsed.is_err() { return Option::None; }
    let v = parsed.unwrap();
    match v {
      JsonValue::Str(s) => Option::Some(s),
      _ => Option::None,
    }
  }

  /// The raw JSON bytes of this slice, as a ref<str>.
  fn raw_json(ref<Self>): ref<str> {
    return self.as_raw_str();
  }

  /// Look up a key in an object slice. Returns None if the slice is not an
  /// object or the key is absent.
  fn get(ref<Self>, key: ref<str>): Option<JsonSlice> {
    let r = self.get_key(key);
    if r.is_err() { return Option::None; }
    return Option::Some(r.unwrap());
  }

  /// Look up an array element by index. Returns None if out of range or not
  /// an array.
  fn index(ref<Self>, i: usize): Option<JsonSlice> {
    let r = self.get_index(i);
    if r.is_err() { return Option::None; }
    return Option::Some(r.unwrap());
  }

  /// Iterator over (key, value) pairs. If the slice is not an object, the
  /// iterator terminates on the first call to `next()`.
  fn object_iter(ref<Self>): JsonObjectIter {
    let pos = self.skip_ws(self.start);
    if pos < self.end && self.data[pos] == 0x7B { pos = pos + 1; }
    return JsonObjectIter { data: self.data, pos: pos, end: self.end };
  }

  /// Iterator over array elements. Terminates immediately if not an array.
  fn array_iter(ref<Self>): JsonArrayIter {
    let pos = self.skip_ws(self.start);
    if pos < self.end && self.data[pos] == 0x5B { pos = pos + 1; }
    return JsonArrayIter { data: self.data, pos: pos, end: self.end };
  }

  /// Materialise this slice into an owned JsonValue tree.
  fn to_owned(ref<Self>): Result<own<JsonValue>, JsonError> {
    return self.parse();
  }

  /// Parse this slice into a full JsonValue (allocating).
  fn parse(ref<Self>): Result<own<JsonValue>, JsonError> {
    let input = self.as_raw_str();
    return json::parse(input);
  }

  /// Return the slice contents as a borrowed string (unsafe — assumes UTF-8).
  fn as_raw_str(ref<Self>): ref<str> {
    return unsafe { str::from_raw_parts(self.data.as_ptr() + self.start, self.end - self.start) };
  }

  /// Get the raw string value (without quotes) as a zero-copy slice.
  fn get_raw_str(ref<Self>): Result<JsonSlice, JsonError> {
    let pos = self.skip_ws(self.start);
    if pos >= self.end || self.data[pos] != 0x22 {
      return Result::Err(JsonError::UnexpectedToken(pos));
    }
    let str_start = pos + 1;
    let i = str_start;
    while i < self.end {
      if self.data[i] == 0x5C { // backslash — skip escaped char
        i = i + 2;
      } else if self.data[i] == 0x22 { // closing quote
        return Result::Ok(JsonSlice::from_range(self.data, str_start, i));
      } else {
        i = i + 1;
      }
    }
    return Result::Err(JsonError::UnexpectedEof);
  }

  /// Lookup a key in a JSON object slice and return a sub-slice for the value.
  fn get_key(ref<Self>, key: ref<str>): Result<JsonSlice, JsonError> {
    let pos = self.skip_ws(self.start);
    if pos >= self.end || self.data[pos] != 0x7B {
      return Result::Err(JsonError::UnexpectedToken(pos));
    }
    pos = pos + 1;
    pos = self.skip_ws(pos);
    if pos < self.end && self.data[pos] == 0x7D {
      return Result::Err(JsonError::UnexpectedToken(pos));
    }
    while pos < self.end {
      pos = self.skip_ws(pos);
      // Parse key string.
      let key_slice = JsonSlice::from_range(self.data, pos, self.end);
      let raw = key_slice.get_raw_str()?;
      let found_key = raw.as_raw_str();
      // Skip past the key string.
      pos = raw.end + 1; // past closing quote
      pos = self.skip_ws(pos);
      if pos >= self.end || self.data[pos] != 0x3A {
        return Result::Err(JsonError::UnexpectedToken(pos));
      }
      pos = pos + 1; // past ':'
      pos = self.skip_ws(pos);
      let val_start = pos;
      pos = self.skip_value(pos)?;
      if found_key == key {
        return Result::Ok(JsonSlice::from_range(self.data, val_start, pos));
      }
      pos = self.skip_ws(pos);
      if pos < self.end && self.data[pos] == 0x2C {
        pos = pos + 1;
      }
    }
    return Result::Err(JsonError::UnexpectedEof);
  }

  /// Lookup an index in a JSON array slice.
  fn get_index(ref<Self>, idx: usize): Result<JsonSlice, JsonError> {
    let pos = self.skip_ws(self.start);
    if pos >= self.end || self.data[pos] != 0x5B {
      return Result::Err(JsonError::UnexpectedToken(pos));
    }
    pos = pos + 1;
    let current: usize = 0;
    while pos < self.end {
      pos = self.skip_ws(pos);
      if self.data[pos] == 0x5D { break; }
      let val_start = pos;
      pos = self.skip_value(pos)?;
      if current == idx {
        return Result::Ok(JsonSlice::from_range(self.data, val_start, pos));
      }
      current = current + 1;
      pos = self.skip_ws(pos);
      if pos < self.end && self.data[pos] == 0x2C {
        pos = pos + 1;
      }
    }
    return Result::Err(JsonError::UnexpectedToken(pos));
  }

  // --- Internal helpers ---

  /// First non-whitespace byte in the slice, or None if empty.
  fn peek_tag(ref<Self>): Option<u8> {
    let p = self.skip_ws(self.start);
    if p >= self.end { return Option::None; }
    return Option::Some(self.data[p]);
  }

  fn skip_ws(ref<Self>, pos: usize): usize {
    let p = pos;
    while p < self.end {
      let c = self.data[p];
      if c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D { p = p + 1; }
      else { break; }
    }
    return p;
  }

  /// Skip over a complete JSON value starting at pos, return end position.
  fn skip_value(ref<Self>, pos: usize): Result<usize, JsonError> {
    let p = self.skip_ws(pos);
    if p >= self.end { return Result::Err(JsonError::UnexpectedEof); }
    let c = self.data[p];
    if c == 0x22 { return self.skip_string(p); }
    if c == 0x7B { return self.skip_container(p, 0x7B, 0x7D); }
    if c == 0x5B { return self.skip_container(p, 0x5B, 0x5D); }
    if c == 0x74 { return Result::Ok(p + 4); } // true
    if c == 0x66 { return Result::Ok(p + 5); } // false
    if c == 0x6E { return Result::Ok(p + 4); } // null
    // number
    while p < self.end {
      let ch = self.data[p];
      if ch == 0x2C || ch == 0x5D || ch == 0x7D || ch == 0x20 || ch == 0x0A || ch == 0x0D || ch == 0x09 {
        break;
      }
      p = p + 1;
    }
    return Result::Ok(p);
  }

  fn skip_string(ref<Self>, pos: usize): Result<usize, JsonError> {
    let p = pos + 1; // past opening quote
    while p < self.end {
      if self.data[p] == 0x5C { p = p + 2; }
      else if self.data[p] == 0x22 { return Result::Ok(p + 1); }
      else { p = p + 1; }
    }
    return Result::Err(JsonError::UnexpectedEof);
  }

  fn skip_container(ref<Self>, pos: usize, open: u8, close: u8): Result<usize, JsonError> {
    let depth: usize = 1;
    let p = pos + 1;
    let in_string = false;
    while p < self.end && depth > 0 {
      let c = self.data[p];
      if in_string {
        if c == 0x5C { p = p + 1; }
        else if c == 0x22 { in_string = false; }
      } else {
        if c == 0x22 { in_string = true; }
        else if c == open { depth = depth + 1; }
        else if c == close { depth = depth - 1; }
      }
      p = p + 1;
    }
    if depth != 0 { return Result::Err(JsonError::UnexpectedEof); }
    return Result::Ok(p);
  }
}

// --- Iterator implementations ---

impl Iterator for JsonObjectIter {
  type Item = (JsonSlice, JsonSlice);
  fn next(refmut<Self>): Option<(JsonSlice, JsonSlice)> {
    let view = JsonSlice::from_range(self.data, self.pos, self.end);
    let p = view.skip_ws(self.pos);
    if p >= self.end || self.data[p] == 0x7D { return Option::None; }
    if self.data[p] == 0x2C { p = view.skip_ws(p + 1); }

    let key_view = JsonSlice::from_range(self.data, p, self.end);
    let raw = key_view.get_raw_str();
    if raw.is_err() { return Option::None; }
    let key_slice = raw.unwrap();

    p = view.skip_ws(key_slice.end + 1);
    if p >= self.end || self.data[p] != 0x3A { return Option::None; }
    p = view.skip_ws(p + 1);

    let val_start = p;
    let end_r = view.skip_value(p);
    if end_r.is_err() { return Option::None; }
    let val_end = end_r.unwrap();

    self.pos = val_end;
    return Option::Some((key_slice, JsonSlice::from_range(self.data, val_start, val_end)));
  }
}

impl Iterator for JsonArrayIter {
  type Item = JsonSlice;
  fn next(refmut<Self>): Option<JsonSlice> {
    let view = JsonSlice::from_range(self.data, self.pos, self.end);
    let p = view.skip_ws(self.pos);
    if p >= self.end || self.data[p] == 0x5D { return Option::None; }
    if self.data[p] == 0x2C { p = view.skip_ws(p + 1); }

    let val_start = p;
    let end_r = view.skip_value(p);
    if end_r.is_err() { return Option::None; }
    let val_end = end_r.unwrap();

    self.pos = val_end;
    return Option::Some(JsonSlice::from_range(self.data, val_start, val_end));
  }
}
