// std/path/posix.ts — POSIX path operations (RFC-0019)

/// Path separator for POSIX systems.
const SEPARATOR: char = '/';
const SEPARATOR_BYTE: u8 = 0x2F;

/// Return the last component of a path.
function basename(path: ref<str>): own<String> {
  let bytes = path.as_bytes();
  let len = bytes.len();
  if len == 0 { return String::from("."); }

  // Strip trailing slashes.
  let end = len;
  while end > 1 && bytes[end - 1] == SEPARATOR_BYTE {
    end = end - 1;
  }
  if end == 1 && bytes[0] == SEPARATOR_BYTE {
    return String::from("/");
  }

  // Find last slash.
  let i = end;
  while i > 0 {
    i = i - 1;
    if bytes[i] == SEPARATOR_BYTE {
      return String::from(path.slice(i + 1, end));
    }
  }
  return String::from(path.slice(0, end));
}

/// Return the directory part of a path.
function dirname(path: ref<str>): own<String> {
  let bytes = path.as_bytes();
  let len = bytes.len();
  if len == 0 { return String::from("."); }

  // Strip trailing slashes (but not the root slash).
  let end = len;
  while end > 1 && bytes[end - 1] == SEPARATOR_BYTE {
    end = end - 1;
  }

  // Find last slash.
  let i = end;
  while i > 0 {
    i = i - 1;
    if bytes[i] == SEPARATOR_BYTE {
      if i == 0 { return String::from("/"); }
      // Strip trailing slashes from dirname.
      let dir_end = i;
      while dir_end > 1 && bytes[dir_end - 1] == SEPARATOR_BYTE {
        dir_end = dir_end - 1;
      }
      return String::from(path.slice(0, dir_end));
    }
  }
  return String::from(".");
}

/// Return the file extension (including the dot), or empty string.
function extname(path: ref<str>): own<String> {
  let base = basename(path);
  let bytes = base.as_str().as_bytes();
  let len = bytes.len();
  // Search from end for '.'.
  let i = len;
  while i > 0 {
    i = i - 1;
    if bytes[i] == 0x2E { // '.'
      if i == 0 { return String::from(""); } // dotfile, no extension
      return String::from(base.as_str().slice(i, len));
    }
  }
  return String::from("");
}

/// Join multiple path segments with the separator.
function join(parts: ref<[ref<str>]>): own<String> {
  let result = String::new();
  let i: usize = 0;
  while i < parts.len() {
    let part = parts[i];
    if part.len() == 0 {
      i = i + 1;
      continue;
    }
    if part.as_bytes()[0] == SEPARATOR_BYTE {
      // Absolute path resets.
      result.clear();
      result.push_str(part);
    } else {
      if result.len() > 0 && result.as_str().as_bytes()[result.len() - 1] != SEPARATOR_BYTE {
        result.push(SEPARATOR);
      }
      result.push_str(part);
    }
    i = i + 1;
  }
  return normalize(result.as_str());
}

/// Normalize a path: resolve `.`, `..`, and collapse multiple slashes.
function normalize(path: ref<str>): own<String> {
  let bytes = path.as_bytes();
  let len = bytes.len();
  if len == 0 { return String::from("."); }

  let is_abs = bytes[0] == SEPARATOR_BYTE;
  let segments: own<Vec<own<String>>> = Vec::new();
  let start: usize = 0;
  let i: usize = 0;

  while i <= len {
    if i == len || bytes[i] == SEPARATOR_BYTE {
      if i > start {
        let seg = path.slice(start, i);
        if seg == ".." {
          if segments.len() > 0 && segments.last().unwrap().as_str() != ".." {
            segments.pop();
          } else if !is_abs {
            segments.push(String::from(".."));
          }
        } else if seg != "." {
          segments.push(String::from(seg));
        }
      }
      start = i + 1;
    }
    i = i + 1;
  }

  let result = String::new();
  if is_abs { result.push(SEPARATOR); }
  let j: usize = 0;
  while j < segments.len() {
    if j > 0 { result.push(SEPARATOR); }
    result.push_str(segments.get(j).unwrap().as_str());
    j = j + 1;
  }

  if result.is_empty() { return String::from("."); }
  return result;
}

/// Check if a path is absolute.
function is_absolute(path: ref<str>): bool {
  let bytes = path.as_bytes();
  return bytes.len() > 0 && bytes[0] == SEPARATOR_BYTE;
}

/// Resolve a sequence of path segments into an absolute path.
/// If no segment is absolute, the result is relative to the current working
/// directory (represented as "/").
function resolve(parts: ref<[ref<str>]>): own<String> {
  let result = String::new();
  // Walk segments right-to-left; the first absolute one anchors.
  let i = parts.len();
  while i > 0 {
    i = i - 1;
    let part = parts[i];
    if part.len() == 0 { continue; }
    if result.len() > 0 {
      let tmp = String::from(part);
      tmp.push(SEPARATOR);
      tmp.push_str(result.as_str());
      result = tmp;
    } else {
      result = String::from(part);
    }
    if part.as_bytes()[0] == SEPARATOR_BYTE {
      // Hit an absolute segment — stop.
      return normalize(result.as_str());
    }
  }
  // No absolute segment found — prepend root.
  let abs = String::from("/");
  if result.len() > 0 {
    abs.push_str(result.as_str());
  }
  return normalize(abs.as_str());
}

/// Replace the extension of a path. `ext` should include the leading dot,
/// e.g. ".md". Pass "" to strip the extension entirely.
function with_extname(path: ref<str>, ext: ref<str>): own<String> {
  let dir = dirname(path);
  let base = basename(path);
  // Strip old extension from basename.
  let old_ext = extname(base.as_str());
  let stem_len = base.len() - old_ext.len();
  let stem = base.as_str().slice(0, stem_len);

  let result = String::from(dir.as_str());
  if result.as_str() != "/" && result.as_str() != "." || dir.as_str() != "." {
    if dir.as_str() != "." {
      result.push(SEPARATOR);
    } else {
      result = String::new();
    }
  }
  result.push_str(stem);
  result.push_str(ext);
  return result;
}

/// Replace the basename (file name) component of a path.
function with_basename(path: ref<str>, name: ref<str>): own<String> {
  let dir = dirname(path);
  if dir.as_str() == "/" {
    let result = String::from("/");
    result.push_str(name);
    return result;
  }
  let result = String::from(dir.as_str());
  result.push(SEPARATOR);
  result.push_str(name);
  return result;
}

/// Compute relative path from `from` to `to`.
function relative(from: ref<str>, to: ref<str>): own<String> {
  let norm_from = normalize(from);
  let norm_to = normalize(to);

  let from_parts = norm_from.as_str().split('/');
  let to_parts = norm_to.as_str().split('/');

  // Find common prefix length.
  let common: usize = 0;
  let max = from_parts.len();
  if to_parts.len() < max { max = to_parts.len(); }
  while common < max {
    if from_parts.get(common).unwrap().as_str() != to_parts.get(common).unwrap().as_str() {
      break;
    }
    common = common + 1;
  }

  let result = String::new();
  // Add ".." for each remaining segment in `from`.
  let ups = from_parts.len() - common;
  let u: usize = 0;
  while u < ups {
    if u > 0 { result.push(SEPARATOR); }
    result.push_str("..");
    u = u + 1;
  }

  // Add remaining segments from `to`.
  let t = common;
  while t < to_parts.len() {
    if result.len() > 0 { result.push(SEPARATOR); }
    result.push_str(to_parts.get(t).unwrap().as_str());
    t = t + 1;
  }

  if result.is_empty() { return String::from("."); }
  return result;
}

/// Extract basename without extension.
/// "baz.ts" → "baz", ".hidden" → ".hidden"
function stem(path: ref<str>): own<String> {
  let base = basename(path);
  let bytes = base.as_str().as_bytes();
  let len = bytes.len();
  let i = len;
  while i > 0 {
    i = i - 1;
    if bytes[i] == 0x2E {
      if i == 0 { return base; }
      return String::from(base.as_str().slice(0, i));
    }
  }
  return base;
}

/// Check if a path is relative (not absolute).
function is_relative(path: ref<str>): bool {
  return !is_absolute(path);
}

/// Check if a path has a trailing separator.
function has_trailing_sep(path: ref<str>): bool {
  let bytes = path.as_bytes();
  return bytes.len() > 0 && bytes[bytes.len() - 1] == SEPARATOR_BYTE;
}

/// Platform separator string.
const SEP_STR: ref<str> = "/";

/// Convert all backslashes to forward slashes.
function to_posix_sep(path: ref<str>): own<String> {
  let result = String::with_capacity(path.len());
  let bytes = path.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    if bytes[i] == 0x5C {
      result.push('/');
    } else {
      result.push(bytes[i] as char);
    }
    i = i + 1;
  }
  return result;
}
