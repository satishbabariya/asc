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

/// Replace (or add) the extension of a path. `ext` may include the leading
/// dot or omit it — both `"md"` and `".md"` produce the same result. Pass
/// the empty string to strip the extension entirely.
///
///   with_extname("foo.ts", "js")     → "foo.js"
///   with_extname("foo", ".js")       → "foo.js"
///   with_extname("/a/b/c.txt", "md") → "/a/b/c.md"
///   with_extname("foo.ts", "")       → "foo"
function with_extname(path: ref<str>, ext: ref<str>): own<String> {
  let dir = dirname(path);
  let base = basename(path);
  // Strip old extension from basename (extname includes the leading dot).
  let old_ext = extname(base.as_str());
  let stem_len = base.len() - old_ext.len();
  let stem = base.as_str().slice(0, stem_len);

  // Prefix the directory. "." means "no directory", so emit nothing — otherwise
  // append the dir and a separator (root "/" already ends in a separator).
  let result = String::new();
  if dir.as_str() != "." {
    result.push_str(dir.as_str());
    if !has_trailing_sep(result.as_str()) { result.push(SEPARATOR); }
  }
  result.push_str(stem);

  // Accept ext with or without the leading dot. Empty ext strips.
  if ext.len() > 0 {
    if ext.as_bytes()[0] != 0x2E {
      result.push('.');
    }
    result.push_str(ext);
  }
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

/// Match a bracket expression `[...]` starting at `pattern[pi+1]` against the
/// byte `c`. Returns a tuple `(matched, end_index)` where `end_index` is the
/// index immediately after the closing `]`. If the bracket expression is
/// malformed (no closing `]`), the `[` is treated as a literal.
///
/// Supports `[abc]`, `[!abc]` / `[^abc]` for negation, and ranges `[a-z]`.
function glob_bracket_match(
  pattern: ref<[u8]>,
  pi: usize,
  plen: usize,
  c: u8,
): (bool, usize) {
  let p = pi + 1;
  let negate = false;
  if p < plen && (pattern[p] == 0x21 || pattern[p] == 0x5E) { // '!' or '^'
    negate = true;
    p = p + 1;
  }
  let matched = false;
  let first = true;
  // Find the closing ']'. Per POSIX, a ']' as the first char is literal.
  while p < plen {
    let ch = pattern[p];
    if ch == 0x5D && !first { // ']'
      if negate { matched = !matched; }
      return (matched, p + 1);
    }
    // Range: a-b (but not at the end, which would be a literal '-').
    if p + 2 < plen && pattern[p + 1] == 0x2D && pattern[p + 2] != 0x5D {
      let lo = ch;
      let hi = pattern[p + 2];
      if c >= lo && c <= hi { matched = true; }
      p = p + 3;
    } else {
      if c == ch { matched = true; }
      p = p + 1;
    }
    first = false;
  }
  // Unterminated '[' — not a valid class; treat the '[' as literal.
  return (c == 0x5B, pi + 1);
}

/// Test whether `path` matches the glob `pattern`.
///
/// Supported syntax:
///   `*`         matches any run of characters except `/`
///   `**`        matches any run of characters including `/`
///   `?`         matches a single non-`/` character
///   `[abc]`     character class (supports ranges `[a-z]` and negation `[!abc]`)
///
/// This is a pure string match — it does **not** touch the filesystem.
function glob_match(pattern: ref<str>, path: ref<str>): bool {
  let p_bytes = pattern.as_bytes();
  let s_bytes = path.as_bytes();
  let plen = p_bytes.len();
  let slen = s_bytes.len();

  // Backtracking state for `*` / `**`.
  let pi: usize = 0;
  let si: usize = 0;
  let star_pi: usize = 0;
  let star_si: usize = 0;
  let has_star = false;
  let star_crosses_sep = false;

  while si < slen {
    if pi < plen {
      let pc = p_bytes[pi];
      if pc == 0x2A { // '*'
        // Detect doubled star `**` — matches across `/`.
        let is_double = pi + 1 < plen && p_bytes[pi + 1] == 0x2A;
        star_crosses_sep = is_double;
        star_pi = pi;
        star_si = si;
        has_star = true;
        if is_double {
          pi = pi + 2;
          // Skip an immediately following '/' so "**/" matches zero segments.
          if pi < plen && p_bytes[pi] == SEPARATOR_BYTE { pi = pi + 1; }
        } else {
          pi = pi + 1;
        }
        continue;
      }
      if pc == 0x3F { // '?'
        if s_bytes[si] != SEPARATOR_BYTE {
          pi = pi + 1;
          si = si + 1;
          continue;
        }
      } else if pc == 0x5B { // '['
        let bm = glob_bracket_match(p_bytes, pi, plen, s_bytes[si]);
        if bm.0 {
          pi = bm.1;
          si = si + 1;
          continue;
        }
      } else if pc == 0x5C { // '\\' — escape the next character literally.
        if pi + 1 < plen && p_bytes[pi + 1] == s_bytes[si] {
          pi = pi + 2;
          si = si + 1;
          continue;
        }
      } else if pc == s_bytes[si] {
        pi = pi + 1;
        si = si + 1;
        continue;
      }
    }
    // Mismatch — try to extend a previous star, if any.
    if has_star {
      // A single `*` must not cross '/'.
      if !star_crosses_sep && s_bytes[star_si] == SEPARATOR_BYTE {
        return false;
      }
      star_si = star_si + 1;
      si = star_si;
      // Re-anchor pi at the pattern position after the star (or `**/`).
      let rp = star_pi;
      if star_crosses_sep {
        rp = rp + 2;
        if rp < plen && p_bytes[rp] == SEPARATOR_BYTE { rp = rp + 1; }
      } else {
        rp = rp + 1;
      }
      pi = rp;
      continue;
    }
    return false;
  }

  // Consume trailing `*` / `**` / `**/` in the pattern.
  while pi < plen {
    if p_bytes[pi] == 0x2A {
      if pi + 1 < plen && p_bytes[pi + 1] == 0x2A {
        pi = pi + 2;
        if pi < plen && p_bytes[pi] == SEPARATOR_BYTE { pi = pi + 1; }
      } else {
        pi = pi + 1;
      }
      continue;
    }
    break;
  }
  return pi == plen;
}
