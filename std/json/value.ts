// std/json/value.ts — JsonValue type (RFC-0016)

/// Error type for JSON operations.
enum JsonError {
  UnexpectedToken(usize),
  UnexpectedEof,
  InvalidNumber(usize),
  InvalidEscape(usize),
  InvalidUnicode(usize),
  TrailingComma(usize),
  DuplicateKey(own<String>),
}

impl Display for JsonError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    match self {
      JsonError::UnexpectedToken(pos) => f.write_str("unexpected token at position ").and_then(|_| pos.fmt(f)),
      JsonError::UnexpectedEof => f.write_str("unexpected end of input"),
      JsonError::InvalidNumber(pos) => f.write_str("invalid number at position ").and_then(|_| pos.fmt(f)),
      JsonError::InvalidEscape(pos) => f.write_str("invalid escape at position ").and_then(|_| pos.fmt(f)),
      JsonError::InvalidUnicode(pos) => f.write_str("invalid unicode at position ").and_then(|_| pos.fmt(f)),
      JsonError::TrailingComma(pos) => f.write_str("trailing comma at position ").and_then(|_| pos.fmt(f)),
      JsonError::DuplicateKey(key) => f.write_str("duplicate key: ").and_then(|_| f.write_str(key.as_str())),
    }
  }
}

/// Represents any JSON value.
enum JsonValue {
  Null,
  Bool(bool),
  Int(i64),
  Uint(u64),
  Float(f64),
  Str(own<String>),
  Array(own<Vec<JsonValue>>),
  Object(own<Vec<(own<String>, JsonValue)>>),
}

impl JsonValue {
  fn is_null(ref<Self>): bool {
    match self { JsonValue::Null => true, _ => false }
  }

  fn is_bool(ref<Self>): bool {
    match self { JsonValue::Bool(_) => true, _ => false }
  }

  fn is_number(ref<Self>): bool {
    match self {
      JsonValue::Int(_) => true,
      JsonValue::Uint(_) => true,
      JsonValue::Float(_) => true,
      _ => false,
    }
  }

  fn is_string(ref<Self>): bool {
    match self { JsonValue::Str(_) => true, _ => false }
  }

  fn is_array(ref<Self>): bool {
    match self { JsonValue::Array(_) => true, _ => false }
  }

  fn is_object(ref<Self>): bool {
    match self { JsonValue::Object(_) => true, _ => false }
  }

  fn as_bool(ref<Self>): Option<bool> {
    match self { JsonValue::Bool(v) => Option::Some(*v), _ => Option::None }
  }

  fn as_i64(ref<Self>): Option<i64> {
    match self {
      JsonValue::Int(v) => Option::Some(*v),
      JsonValue::Uint(v) => {
        if *v <= 9223372036854775807 { Option::Some(*v as i64) }
        else { Option::None }
      },
      _ => Option::None,
    }
  }

  fn as_u64(ref<Self>): Option<u64> {
    match self {
      JsonValue::Uint(v) => Option::Some(*v),
      JsonValue::Int(v) => {
        if *v >= 0 { Option::Some(*v as u64) }
        else { Option::None }
      },
      _ => Option::None,
    }
  }

  fn as_f64(ref<Self>): Option<f64> {
    match self {
      JsonValue::Float(v) => Option::Some(*v),
      JsonValue::Int(v) => Option::Some(*v as f64),
      JsonValue::Uint(v) => Option::Some(*v as f64),
      _ => Option::None,
    }
  }

  fn as_str(ref<Self>): Option<ref<str>> {
    match self { JsonValue::Str(s) => Option::Some(s.as_str()), _ => Option::None }
  }

  fn as_array(ref<Self>): Option<ref<Vec<JsonValue>>> {
    match self { JsonValue::Array(a) => Option::Some(a), _ => Option::None }
  }

  fn as_array_mut(refmut<Self>): Option<refmut<Vec<JsonValue>>> {
    match self { JsonValue::Array(a) => Option::Some(a), _ => Option::None }
  }

  fn as_object(ref<Self>): Option<ref<Vec<(own<String>, JsonValue)>>> {
    match self { JsonValue::Object(o) => Option::Some(o), _ => Option::None }
  }

  fn as_object_mut(refmut<Self>): Option<refmut<Vec<(own<String>, JsonValue)>>> {
    match self { JsonValue::Object(o) => Option::Some(o), _ => Option::None }
  }

  /// Lookup a key in a JSON object. Returns None if not an object or key not found.
  fn get(ref<Self>, key: ref<str>): Option<ref<JsonValue>> {
    match self {
      JsonValue::Object(entries) => {
        let i: usize = 0;
        while i < entries.len() {
          let entry = entries.get(i).unwrap();
          if entry.0.as_str() == key {
            return Option::Some(&entry.1);
          }
          i = i + 1;
        }
        return Option::None;
      },
      _ => Option::None,
    }
  }

  /// Lookup by index in a JSON array.
  fn index(ref<Self>, idx: usize): Option<ref<JsonValue>> {
    match self {
      JsonValue::Array(arr) => arr.get(idx),
      _ => Option::None,
    }
  }
}

impl Drop for JsonValue {
  fn drop(refmut<Self>): void {
    // Enum variants with owned fields are dropped automatically by the compiler.
  }
}

impl Clone for JsonValue {
  fn clone(ref<Self>): own<JsonValue> {
    match self {
      JsonValue::Null => JsonValue::Null,
      JsonValue::Bool(v) => JsonValue::Bool(*v),
      JsonValue::Int(v) => JsonValue::Int(*v),
      JsonValue::Uint(v) => JsonValue::Uint(*v),
      JsonValue::Float(v) => JsonValue::Float(*v),
      JsonValue::Str(s) => JsonValue::Str(s.clone()),
      JsonValue::Array(arr) => {
        let new_arr = Vec::with_capacity(arr.len());
        let i: usize = 0;
        while i < arr.len() {
          new_arr.push(arr.get(i).unwrap().clone());
          i = i + 1;
        }
        return JsonValue::Array(new_arr);
      },
      JsonValue::Object(entries) => {
        let new_entries: own<Vec<(own<String>, JsonValue)>> = Vec::with_capacity(entries.len());
        let i: usize = 0;
        while i < entries.len() {
          let entry = entries.get(i).unwrap();
          new_entries.push((entry.0.clone(), entry.1.clone()));
          i = i + 1;
        }
        return JsonValue::Object(new_entries);
      },
    }
  }
}
