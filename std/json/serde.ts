// std/json/serde.ts — Serialize/Deserialize traits for JSON (RFC-0016)

import { JsonValue, JsonError } from './value';

/// Trait for types that can be serialized to JSON.
trait Serialize {
  fn to_json(ref<Self>): own<JsonValue>;
}

/// Trait for types that can be deserialized from JSON.
trait Deserialize {
  fn from_json(value: ref<JsonValue>): Result<own<Self>, JsonError>;
}

// --- Primitive implementations ---

impl Serialize for bool {
  fn to_json(ref<Self>): own<JsonValue> {
    return JsonValue::Bool(*self);
  }
}

impl Deserialize for bool {
  fn from_json(value: ref<JsonValue>): Result<own<bool>, JsonError> {
    match value {
      JsonValue::Bool(v) => Result::Ok(*v),
      _ => Result::Err(JsonError::UnexpectedToken(0)),
    }
  }
}

impl Serialize for i64 {
  fn to_json(ref<Self>): own<JsonValue> {
    return JsonValue::Int(*self);
  }
}

impl Deserialize for i64 {
  fn from_json(value: ref<JsonValue>): Result<own<i64>, JsonError> {
    match value.as_i64() {
      Option::Some(v) => Result::Ok(v),
      Option::None => Result::Err(JsonError::UnexpectedToken(0)),
    }
  }
}

impl Serialize for u64 {
  fn to_json(ref<Self>): own<JsonValue> {
    return JsonValue::Uint(*self);
  }
}

impl Deserialize for u64 {
  fn from_json(value: ref<JsonValue>): Result<own<u64>, JsonError> {
    match value.as_u64() {
      Option::Some(v) => Result::Ok(v),
      Option::None => Result::Err(JsonError::UnexpectedToken(0)),
    }
  }
}

impl Serialize for f64 {
  fn to_json(ref<Self>): own<JsonValue> {
    return JsonValue::Float(*self);
  }
}

impl Deserialize for f64 {
  fn from_json(value: ref<JsonValue>): Result<own<f64>, JsonError> {
    match value.as_f64() {
      Option::Some(v) => Result::Ok(v),
      Option::None => Result::Err(JsonError::UnexpectedToken(0)),
    }
  }
}

impl Serialize for String {
  fn to_json(ref<Self>): own<JsonValue> {
    return JsonValue::Str(self.clone());
  }
}

impl Deserialize for String {
  fn from_json(value: ref<JsonValue>): Result<own<String>, JsonError> {
    match value.as_str() {
      Option::Some(s) => Result::Ok(String::from(s)),
      Option::None => Result::Err(JsonError::UnexpectedToken(0)),
    }
  }
}

impl<T: Serialize> Serialize for Vec<T> {
  fn to_json(ref<Self>): own<JsonValue> {
    let arr: own<Vec<JsonValue>> = Vec::with_capacity(self.len());
    let i: usize = 0;
    while i < self.len() {
      arr.push(self.get(i).unwrap().to_json());
      i = i + 1;
    }
    return JsonValue::Array(arr);
  }
}

impl<T: Deserialize> Deserialize for Vec<T> {
  fn from_json(value: ref<JsonValue>): Result<own<Vec<T>>, JsonError> {
    match value.as_array() {
      Option::Some(arr) => {
        let result: own<Vec<T>> = Vec::with_capacity(arr.len());
        let i: usize = 0;
        while i < arr.len() {
          let item = T::from_json(arr.get(i).unwrap())?;
          result.push(item);
          i = i + 1;
        }
        return Result::Ok(result);
      },
      Option::None => Result::Err(JsonError::UnexpectedToken(0)),
    }
  }
}

impl<T: Serialize> Serialize for Option<T> {
  fn to_json(ref<Self>): own<JsonValue> {
    match self {
      Option::Some(v) => v.to_json(),
      Option::None => JsonValue::Null,
    }
  }
}

impl<T: Deserialize> Deserialize for Option<T> {
  fn from_json(value: ref<JsonValue>): Result<own<Option<T>>, JsonError> {
    if value.is_null() {
      return Result::Ok(Option::None);
    }
    let inner = T::from_json(value)?;
    return Result::Ok(Option::Some(inner));
  }
}
