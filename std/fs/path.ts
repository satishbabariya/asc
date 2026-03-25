// std/fs/path.ts — Path manipulation (RFC-0014, RFC-0019)

/// An owned filesystem path. Wraps a String internally.
struct Path {
  inner: own<String>,
}

impl Path {
  /// Creates a Path by borrowing a string slice (zero-copy view).
  fn from_str(s: ref<str>): ref<Path> {
    // Path is layout-compatible with str for borrow purposes.
    return unsafe { &*(s as *const str as *const Path) };
  }

  /// Creates an owned Path from an owned String.
  fn new(s: own<String>): own<Path> {
    return Path { inner: s };
  }

  /// Returns the path as a string slice.
  fn as_str(ref<Self>): ref<str> {
    return self.inner.as_str();
  }

  /// Joins this path with another component, separated by '/'.
  fn join(ref<Self>, other: ref<str>): own<Path> {
    let result = self.inner.clone();
    const s = self.as_str();
    if s.len() > 0 && !s.ends_with("/") {
      result.push('/');
    }
    result.push_str(other);
    return Path { inner: result };
  }

  /// Returns the final component of the path (after the last '/').
  fn file_name(ref<Self>): Option<ref<str>> {
    const s = self.as_str();
    const len = s.len();
    if len == 0 { return Option::None; }

    // Find the last '/'.
    let i = len;
    while i > 0 {
      i = i - 1;
      if s.byte_at(i) == 47 {  // '/'
        if i + 1 < len {
          return Option::Some(s.slice(i + 1, len));
        }
        return Option::None;
      }
    }
    // No separator — entire path is the file name.
    return Option::Some(s);
  }

  /// Returns the file stem (file_name without extension).
  fn file_stem(ref<Self>): Option<ref<str>> {
    match self.file_name() {
      Option::None => { return Option::None; },
      Option::Some(name) => {
        const name_len = name.len();
        let dot_pos = name_len;
        let i = name_len;
        while i > 0 {
          i = i - 1;
          if name.byte_at(i) == 46 {  // '.'
            dot_pos = i;
            break;
          }
        }
        if dot_pos == 0 || dot_pos == name_len {
          return Option::Some(name);
        }
        return Option::Some(name.slice(0, dot_pos));
      },
    }
  }

  /// Returns the file extension (after the last '.'), or None.
  fn extension(ref<Self>): Option<ref<str>> {
    match self.file_name() {
      Option::None => { return Option::None; },
      Option::Some(name) => {
        const name_len = name.len();
        let i = name_len;
        while i > 0 {
          i = i - 1;
          if name.byte_at(i) == 46 {  // '.'
            if i == 0 { return Option::None; }
            return Option::Some(name.slice(i + 1, name_len));
          }
        }
        return Option::None;
      },
    }
  }

  /// Returns the parent directory, or None if this is a root or empty path.
  fn parent(ref<Self>): Option<ref<Path>> {
    const s = self.as_str();
    const len = s.len();
    if len == 0 { return Option::None; }

    // Find the last '/'.
    let i = len;
    while i > 0 {
      i = i - 1;
      if s.byte_at(i) == 47 {  // '/'
        if i == 0 {
          return Option::Some(Path::from_str("/"));
        }
        return Option::Some(Path::from_str(s.slice(0, i)));
      }
    }
    return Option::None;
  }

  /// Returns true if the path starts with '/'.
  fn is_absolute(ref<Self>): bool {
    const s = self.as_str();
    if s.len() == 0 { return false; }
    return s.byte_at(0) == 47;  // '/'
  }

  /// Returns true if the path does not start with '/'.
  fn is_relative(ref<Self>): bool {
    return !self.is_absolute();
  }

  /// Returns true if the path has an extension.
  fn has_extension(ref<Self>): bool {
    return self.extension().is_some();
  }

  /// Returns the path with a new extension appended or replaced.
  fn with_extension(ref<Self>, ext: ref<str>): own<Path> {
    let base = match self.file_stem() {
      Option::Some(stem) => {
        match self.parent() {
          Option::Some(parent) => parent.join(stem),
          Option::None => Path::new(String::from(stem)),
        }
      },
      Option::None => Path::new(self.inner.clone()),
    };
    if ext.len() > 0 {
      base.inner.push('.');
      base.inner.push_str(ext);
    }
    return base;
  }
}

impl Display for Path {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    return f.write_str(self.as_str());
  }
}

impl Clone for Path {
  fn clone(ref<Self>): own<Path> {
    return Path { inner: self.inner.clone() };
  }
}

impl PartialEq for Path {
  fn eq(ref<Self>, other: ref<Path>): bool {
    return self.inner.eq(&other.inner);
  }
}

impl Drop for Path {
  fn drop(refmut<Self>): void {
    // inner (owned String) is dropped automatically.
  }
}
