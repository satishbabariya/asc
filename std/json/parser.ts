// std/json/parser.ts — JSON parser (RFC-0016)

import { JsonValue, JsonError } from './value';

/// Internal parser state holding the input and current position.
struct JsonParser {
  input: ref<str>,
  pos: usize,
}

/// Parse a JSON string into a JsonValue.
function parse(input: ref<str>): Result<own<JsonValue>, JsonError> {
  let parser = JsonParser { input: input, pos: 0 };
  parser.skip_whitespace();
  let value = parser.parse_value()?;
  parser.skip_whitespace();
  if parser.pos < parser.input.len() {
    return Result::Err(JsonError::UnexpectedToken(parser.pos));
  }
  return Result::Ok(value);
}

impl JsonParser {
  fn peek(ref<Self>): Option<u8> {
    if self.pos >= self.input.len() { return Option::None; }
    return Option::Some(self.input.as_bytes()[self.pos]);
  }

  fn advance(refmut<Self>): u8 {
    let c = self.input.as_bytes()[self.pos];
    self.pos = self.pos + 1;
    return c;
  }

  fn skip_whitespace(refmut<Self>): void {
    while self.pos < self.input.len() {
      let c = self.input.as_bytes()[self.pos];
      if c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D {
        self.pos = self.pos + 1;
      } else {
        break;
      }
    }
  }

  fn expect(refmut<Self>, expected: u8): Result<void, JsonError> {
    if self.pos >= self.input.len() {
      return Result::Err(JsonError::UnexpectedEof);
    }
    if self.input.as_bytes()[self.pos] != expected {
      return Result::Err(JsonError::UnexpectedToken(self.pos));
    }
    self.pos = self.pos + 1;
    return Result::Ok(void);
  }

  fn parse_value(refmut<Self>): Result<own<JsonValue>, JsonError> {
    self.skip_whitespace();
    let c = self.peek();
    if c.is_none() { return Result::Err(JsonError::UnexpectedEof); }
    let ch = c.unwrap();

    if ch == 0x22 { // '"'
      let s = self.parse_string()?;
      return Result::Ok(JsonValue::Str(s));
    }
    if ch == 0x7B { // '{'
      return self.parse_object();
    }
    if ch == 0x5B { // '['
      return self.parse_array();
    }
    if ch == 0x74 { // 't'
      return self.parse_true();
    }
    if ch == 0x66 { // 'f'
      return self.parse_false();
    }
    if ch == 0x6E { // 'n'
      return self.parse_null();
    }
    if ch == 0x2D || (ch >= 0x30 && ch <= 0x39) { // '-' or digit
      return self.parse_number();
    }
    return Result::Err(JsonError::UnexpectedToken(self.pos));
  }

  fn parse_null(refmut<Self>): Result<own<JsonValue>, JsonError> {
    let start = self.pos;
    self.expect(0x6E)?; // n
    self.expect(0x75)?; // u
    self.expect(0x6C)?; // l
    self.expect(0x6C)?; // l
    return Result::Ok(JsonValue::Null);
  }

  fn parse_true(refmut<Self>): Result<own<JsonValue>, JsonError> {
    self.expect(0x74)?; // t
    self.expect(0x72)?; // r
    self.expect(0x75)?; // u
    self.expect(0x65)?; // e
    return Result::Ok(JsonValue::Bool(true));
  }

  fn parse_false(refmut<Self>): Result<own<JsonValue>, JsonError> {
    self.expect(0x66)?; // f
    self.expect(0x61)?; // a
    self.expect(0x6C)?; // l
    self.expect(0x73)?; // s
    self.expect(0x65)?; // e
    return Result::Ok(JsonValue::Bool(false));
  }

  fn parse_number(refmut<Self>): Result<own<JsonValue>, JsonError> {
    let start = self.pos;
    let is_negative = false;
    let is_float = false;

    // Optional minus sign.
    if self.peek() == Option::Some(0x2D) {
      is_negative = true;
      self.advance();
    }

    // Integer part.
    if self.pos >= self.input.len() {
      return Result::Err(JsonError::InvalidNumber(start));
    }
    let first = self.input.as_bytes()[self.pos];
    if first == 0x30 { // '0'
      self.advance();
    } else if first >= 0x31 && first <= 0x39 {
      while self.pos < self.input.len() {
        let c = self.input.as_bytes()[self.pos];
        if c >= 0x30 && c <= 0x39 { self.advance(); }
        else { break; }
      }
    } else {
      return Result::Err(JsonError::InvalidNumber(start));
    }

    // Fractional part.
    if self.pos < self.input.len() && self.input.as_bytes()[self.pos] == 0x2E { // '.'
      is_float = true;
      self.advance();
      if self.pos >= self.input.len() || self.input.as_bytes()[self.pos] < 0x30 || self.input.as_bytes()[self.pos] > 0x39 {
        return Result::Err(JsonError::InvalidNumber(start));
      }
      while self.pos < self.input.len() {
        let c = self.input.as_bytes()[self.pos];
        if c >= 0x30 && c <= 0x39 { self.advance(); }
        else { break; }
      }
    }

    // Exponent part.
    if self.pos < self.input.len() {
      let ec = self.input.as_bytes()[self.pos];
      if ec == 0x65 || ec == 0x45 { // 'e' or 'E'
        is_float = true;
        self.advance();
        if self.pos < self.input.len() {
          let sc = self.input.as_bytes()[self.pos];
          if sc == 0x2B || sc == 0x2D { self.advance(); } // '+' or '-'
        }
        if self.pos >= self.input.len() || self.input.as_bytes()[self.pos] < 0x30 || self.input.as_bytes()[self.pos] > 0x39 {
          return Result::Err(JsonError::InvalidNumber(start));
        }
        while self.pos < self.input.len() {
          let c = self.input.as_bytes()[self.pos];
          if c >= 0x30 && c <= 0x39 { self.advance(); }
          else { break; }
        }
      }
    }

    let num_str = self.input.slice(start, self.pos);
    if is_float {
      let val = parse_f64(num_str);
      return Result::Ok(JsonValue::Float(val));
    }
    if is_negative {
      let val = parse_i64(num_str);
      return Result::Ok(JsonValue::Int(val));
    }
    let val = parse_u64(num_str);
    if val > 9223372036854775807 {
      return Result::Ok(JsonValue::Uint(val));
    }
    return Result::Ok(JsonValue::Int(val as i64));
  }

  fn parse_string(refmut<Self>): Result<own<String>, JsonError> {
    self.expect(0x22)?; // '"'
    let result = String::new();
    while self.pos < self.input.len() {
      let c = self.advance();
      if c == 0x22 { // closing '"'
        return Result::Ok(result);
      }
      if c == 0x5C { // '\'
        if self.pos >= self.input.len() {
          return Result::Err(JsonError::InvalidEscape(self.pos));
        }
        let esc = self.advance();
        if esc == 0x22 { result.push('"' as char); }       // \"
        else if esc == 0x5C { result.push('\\' as char); }  // \\
        else if esc == 0x2F { result.push('/' as char); }   // \/
        else if esc == 0x62 { result.push(0x08 as char); }  // \b
        else if esc == 0x66 { result.push(0x0C as char); }  // \f
        else if esc == 0x6E { result.push(0x0A as char); }  // \n
        else if esc == 0x72 { result.push(0x0D as char); }  // \r
        else if esc == 0x74 { result.push(0x09 as char); }  // \t
        else if esc == 0x75 { // \uXXXX
          let cp = self.parse_hex4()?;
          // Handle surrogate pairs for characters above U+FFFF.
          if cp >= 0xD800 && cp <= 0xDBFF {
            self.expect(0x5C)?; // '\'
            self.expect(0x75)?; // 'u'
            let low = self.parse_hex4()?;
            if low < 0xDC00 || low > 0xDFFF {
              return Result::Err(JsonError::InvalidUnicode(self.pos));
            }
            let full = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            self.encode_utf8(refmut result, full);
          } else {
            self.encode_utf8(refmut result, cp as u32);
          }
        } else {
          return Result::Err(JsonError::InvalidEscape(self.pos - 1));
        }
      } else {
        result.push(c as char);
      }
    }
    return Result::Err(JsonError::UnexpectedEof);
  }

  fn parse_hex4(refmut<Self>): Result<u32, JsonError> {
    let val: u32 = 0;
    let i = 0;
    while i < 4 {
      if self.pos >= self.input.len() {
        return Result::Err(JsonError::InvalidUnicode(self.pos));
      }
      let c = self.advance();
      val = val << 4;
      if c >= 0x30 && c <= 0x39 { val = val | (c - 0x30) as u32; }
      else if c >= 0x41 && c <= 0x46 { val = val | (c - 0x41 + 10) as u32; }
      else if c >= 0x61 && c <= 0x66 { val = val | (c - 0x61 + 10) as u32; }
      else { return Result::Err(JsonError::InvalidUnicode(self.pos - 1)); }
      i = i + 1;
    }
    return Result::Ok(val);
  }

  fn encode_utf8(ref<Self>, buf: refmut<String>, cp: u32): void {
    if cp < 0x80 {
      buf.push(cp as char);
    } else if cp < 0x800 {
      buf.push((0xC0 | (cp >> 6)) as char);
      buf.push((0x80 | (cp & 0x3F)) as char);
    } else if cp < 0x10000 {
      buf.push((0xE0 | (cp >> 12)) as char);
      buf.push((0x80 | ((cp >> 6) & 0x3F)) as char);
      buf.push((0x80 | (cp & 0x3F)) as char);
    } else {
      buf.push((0xF0 | (cp >> 18)) as char);
      buf.push((0x80 | ((cp >> 12) & 0x3F)) as char);
      buf.push((0x80 | ((cp >> 6) & 0x3F)) as char);
      buf.push((0x80 | (cp & 0x3F)) as char);
    }
  }

  fn parse_array(refmut<Self>): Result<own<JsonValue>, JsonError> {
    self.expect(0x5B)?; // '['
    self.skip_whitespace();
    let arr: own<Vec<JsonValue>> = Vec::new();
    if self.peek() == Option::Some(0x5D) { // ']'
      self.advance();
      return Result::Ok(JsonValue::Array(arr));
    }
    loop {
      let value = self.parse_value()?;
      arr.push(value);
      self.skip_whitespace();
      let next = self.peek();
      if next == Option::Some(0x2C) { // ','
        self.advance();
        self.skip_whitespace();
        if self.peek() == Option::Some(0x5D) {
          return Result::Err(JsonError::TrailingComma(self.pos));
        }
      } else if next == Option::Some(0x5D) { // ']'
        self.advance();
        return Result::Ok(JsonValue::Array(arr));
      } else {
        return Result::Err(JsonError::UnexpectedToken(self.pos));
      }
    }
  }

  fn parse_object(refmut<Self>): Result<own<JsonValue>, JsonError> {
    self.expect(0x7B)?; // '{'
    self.skip_whitespace();
    let entries: own<Vec<(own<String>, JsonValue)>> = Vec::new();
    if self.peek() == Option::Some(0x7D) { // '}'
      self.advance();
      return Result::Ok(JsonValue::Object(entries));
    }
    loop {
      self.skip_whitespace();
      let key = self.parse_string()?;
      self.skip_whitespace();
      self.expect(0x3A)?; // ':'
      let value = self.parse_value()?;
      entries.push((key, value));
      self.skip_whitespace();
      let next = self.peek();
      if next == Option::Some(0x2C) { // ','
        self.advance();
        self.skip_whitespace();
        if self.peek() == Option::Some(0x7D) {
          return Result::Err(JsonError::TrailingComma(self.pos));
        }
      } else if next == Option::Some(0x7D) { // '}'
        self.advance();
        return Result::Ok(JsonValue::Object(entries));
      } else {
        return Result::Err(JsonError::UnexpectedToken(self.pos));
      }
    }
  }
}
