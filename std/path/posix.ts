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
