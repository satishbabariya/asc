// std/string.ts — String and str methods (RFC-0013)

/// Owned, growable UTF-8 string. Layout: { ptr: *mut u8, len: usize, cap: usize }.
struct String {
  ptr: *mut u8,
  len: usize,
  cap: usize,
}

impl String {
  fn new(): own<String> {
    return String { ptr: null, len: 0, cap: 0 };
  }

  fn with_capacity(capacity: usize): own<String> {
    const ptr = malloc(capacity) as *mut u8;
    return String { ptr: ptr, len: 0, cap: capacity };
  }

  fn from(s: ref<str>): own<String> {
    const len = s.len();
    const ptr = malloc(len) as *mut u8;
    memcpy(ptr, s.as_ptr(), len);
    return String { ptr: ptr, len: len, cap: len };
  }

  fn as_str(ref<Self>): ref<str> {
    // DECISION: Returns a borrow of the internal buffer as str slice.
    return unsafe { str::from_raw_parts(self.ptr as *const u8, self.len) };
  }

  fn len(ref<Self>): usize { return self.len; }
  fn is_empty(ref<Self>): bool { return self.len == 0; }
  fn capacity(ref<Self>): usize { return self.cap; }

  fn push_str(refmut<Self>, s: ref<str>): void {
    const slen = s.len();
    self.ensure_capacity(self.len + slen);
    memcpy(self.ptr + self.len, s.as_ptr(), slen);
    self.len = self.len + slen;
  }

  fn push(refmut<Self>, c: char): void {
    // DECISION: Encode char as UTF-8 bytes and append.
    // Simplified: only handle ASCII for now.
    self.ensure_capacity(self.len + 4);
    self.ptr[self.len] = c as u8;
    self.len = self.len + 1;
  }

  fn clear(refmut<Self>): void { self.len = 0; }

  fn truncate(refmut<Self>, new_len: usize): void {
    if new_len < self.len { self.len = new_len; }
  }

  fn contains(ref<Self>, pattern: ref<str>): bool {
    return self.as_str().contains(pattern);
  }

  fn starts_with(ref<Self>, prefix: ref<str>): bool {
    return self.as_str().starts_with(prefix);
  }

  fn ends_with(ref<Self>, suffix: ref<str>): bool {
    return self.as_str().ends_with(suffix);
  }

  fn repeat(ref<Self>, n: usize): own<String> {
    let result = String::with_capacity(self.len * n);
    let i: usize = 0;
    while i < n {
      result.push_str(self.as_str());
      i = i + 1;
    }
    return result;
  }

  fn to_uppercase(ref<Self>): own<String> {
    let result = String::with_capacity(self.len);
    let i: usize = 0;
    while i < self.len {
      let c = self.ptr[i];
      if c >= 97 && c <= 122 { c = c - 32; }  // a-z → A-Z
      result.push(c as char);
      i = i + 1;
    }
    return result;
  }

  fn to_lowercase(ref<Self>): own<String> {
    let result = String::with_capacity(self.len);
    let i: usize = 0;
    while i < self.len {
      let c = self.ptr[i];
      if c >= 65 && c <= 90 { c = c + 32; }  // A-Z → a-z
      result.push(c as char);
      i = i + 1;
    }
    return result;
  }

  fn find(ref<Self>, pattern: ref<str>): Option<usize> {
    const plen = pattern.len();
    if plen == 0 { return Option::Some(0); }
    if plen > self.len { return Option::None; }
    let i: usize = 0;
    const limit = self.len - plen + 1;
    const pptr = pattern.as_ptr();
    while i < limit {
      let matched = true;
      let j: usize = 0;
      while j < plen {
        if self.ptr[i + j] != pptr[j] { matched = false; break; }
        j = j + 1;
      }
      if matched { return Option::Some(i); }
      i = i + 1;
    }
    return Option::None;
  }

  fn trim(ref<Self>): ref<str> {
    let start: usize = 0;
    while start < self.len && String::is_ascii_whitespace(self.ptr[start]) {
      start = start + 1;
    }
    let end: usize = self.len;
    while end > start && String::is_ascii_whitespace(self.ptr[end - 1]) {
      end = end - 1;
    }
    return unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, end - start) };
  }

  fn trim_start(ref<Self>): ref<str> {
    let start: usize = 0;
    while start < self.len && String::is_ascii_whitespace(self.ptr[start]) {
      start = start + 1;
    }
    return unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, self.len - start) };
  }

  fn trim_end(ref<Self>): ref<str> {
    let end: usize = self.len;
    while end > 0 && String::is_ascii_whitespace(self.ptr[end - 1]) {
      end = end - 1;
    }
    return unsafe { str::from_raw_parts(self.ptr as *const u8, end) };
  }

  fn is_ascii_whitespace(c: u8): bool {
    return c == 32 || c == 9 || c == 10 || c == 13;
  }

  fn split(ref<Self>, sep: ref<str>): own<Vec<ref<str>>> {
    let result: Vec<ref<str>> = Vec::new();
    const slen = sep.len();
    if slen == 0 {
      result.push(self.as_str());
      return result;
    }
    let start: usize = 0;
    let i: usize = 0;
    const sep_ptr = sep.as_ptr();
    while i + slen <= self.len {
      let matched = true;
      let j: usize = 0;
      while j < slen {
        if self.ptr[i + j] != sep_ptr[j] { matched = false; break; }
        j = j + 1;
      }
      if matched {
        const part = unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, i - start) };
        result.push(part);
        start = i + slen;
        i = start;
      } else {
        i = i + 1;
      }
    }
    const last_part = unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, self.len - start) };
    result.push(last_part);
    return result;
  }

  fn replace(ref<Self>, from: ref<str>, to: ref<str>): own<String> {
    let result = String::new();
    const flen = from.len();
    if flen == 0 {
      result.push_str(self.as_str());
      return result;
    }
    let start: usize = 0;
    let i: usize = 0;
    const from_ptr = from.as_ptr();
    while i + flen <= self.len {
      let matched = true;
      let j: usize = 0;
      while j < flen {
        if self.ptr[i + j] != from_ptr[j] { matched = false; break; }
        j = j + 1;
      }
      if matched {
        const segment = unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, i - start) };
        result.push_str(segment);
        result.push_str(to);
        start = i + flen;
        i = start;
      } else {
        i = i + 1;
      }
    }
    const tail = unsafe { str::from_raw_parts((self.ptr as usize + start) as *const u8, self.len - start) };
    result.push_str(tail);
    return result;
  }

  fn as_bytes(ref<Self>): ref<[u8]> {
    return unsafe { slice::from_raw_parts(self.ptr as *const u8, self.len) };
  }

  // Internal: ensure capacity for at least `min_cap` bytes.
  fn ensure_capacity(refmut<Self>, min_cap: usize): void {
    if min_cap <= self.cap { return; }
    let new_cap = self.cap * 2;
    if new_cap < min_cap { new_cap = min_cap; }
    if new_cap < 8 { new_cap = 8; }
    const new_ptr = malloc(new_cap) as *mut u8;
    if self.ptr != null {
      memcpy(new_ptr, self.ptr, self.len);
      free(self.ptr);
    }
    self.ptr = new_ptr;
    self.cap = new_cap;
  }
}

impl Drop for String {
  fn drop(refmut<Self>): void {
    if self.ptr != null { free(self.ptr); }
  }
}

impl Display for String {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str(self.as_str());
  }
}

impl Clone for String {
  fn clone(ref<Self>): own<String> {
    return String::from(self.as_str());
  }
}

impl PartialEq for String {
  fn eq(ref<Self>, other: ref<String>): bool {
    if self.len != other.len { return false; }
    let i: usize = 0;
    while i < self.len {
      if self.ptr[i] != other.ptr[i] { return false; }
      i = i + 1;
    }
    return true;
  }
}

impl Eq for String {}
