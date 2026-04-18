// std/path/windows.ts — Windows path manipulation (RFC-0019)
//
// DECISION: Windows paths use `\` as the canonical separator but accept `/`
// as well. Drive letters (`C:\`, `C:foo`) and UNC paths (`\\server\share`)
// are recognised. Comparisons are case-insensitive per Windows convention.

/// Path separators accepted by this module.
const BACKSLASH: char = '\\';
const FORWARD_SLASH: char = '/';
const BACKSLASH_BYTE: u8 = 0x5C;
const FORWARD_SLASH_BYTE: u8 = 0x2F;
const COLON_BYTE: u8 = 0x3A;
const DOT_BYTE: u8 = 0x2E;

/// Canonical platform separator string.
const SEP_STR: ref<str> = "\\";

/// True if the byte is either `\` or `/`.
function is_sep_byte(b: u8): bool {
  return b == BACKSLASH_BYTE || b == FORWARD_SLASH_BYTE;
}

/// Uppercase a single ASCII byte; non-letters pass through.
function ascii_upper(b: u8): u8 {
  if b >= 0x61 && b <= 0x7A { return b - 0x20; }
  return b;
}

/// Convert forward slashes to backslashes.
function to_windows(path: ref<str>): own<String> {
  let result = String::with_capacity(path.len());
  let bytes = path.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    if bytes[i] == FORWARD_SLASH_BYTE {
      result.push(BACKSLASH);
    } else {
      result.push(bytes[i] as char);
    }
    i = i + 1;
  }
  return result;
}

/// Convert backslashes to forward slashes.
function to_posix(path: ref<str>): own<String> {
  let result = String::with_capacity(path.len());
  let bytes = path.as_bytes();
  let i: usize = 0;
  while i < bytes.len() {
    if bytes[i] == BACKSLASH_BYTE {
      result.push(FORWARD_SLASH);
    } else {
      result.push(bytes[i] as char);
    }
    i = i + 1;
  }
  return result;
}

/// Return the drive letter (uppercased) of a Windows path, or None.
/// Accepts both `C:` and `c:\foo`.
function drive_letter(path: ref<str>): Option<char> {
  let bytes = path.as_bytes();
  if bytes.len() >= 2 {
    let c0 = bytes[0];
    let c1 = bytes[1];
    let is_letter = (c0 >= 0x41 && c0 <= 0x5A) || (c0 >= 0x61 && c0 <= 0x7A);
    if is_letter && c1 == COLON_BYTE {
      return Option::Some(ascii_upper(c0) as char);
    }
  }
  return Option::None;
}

/// True if `path` starts with a UNC prefix (`\\server\share` or `//server/share`).
function is_unc(path: ref<str>): bool {
  let bytes = path.as_bytes();
  return bytes.len() >= 2 && is_sep_byte(bytes[0]) && is_sep_byte(bytes[1]);
}

/// Length of the path prefix that cannot be stripped by normalisation.
/// Covers drive-with-root (`C:\`), drive-only (`C:`), UNC share root, and
/// the plain root (`\` / `/`). Returns 0 for purely relative paths.
function prefix_len(path: ref<str>): usize {
  let bytes = path.as_bytes();
  let len = bytes.len();
  if len == 0 { return 0; }

  // UNC: \\server\share
  if is_unc(path) {
    // Walk past server segment.
    let i: usize = 2;
    while i < len && !is_sep_byte(bytes[i]) { i = i + 1; }
    if i < len { i = i + 1; } // sep between server and share
    // Walk past share segment.
    while i < len && !is_sep_byte(bytes[i]) { i = i + 1; }
    return i;
  }

  // Drive letter.
  if len >= 2 {
    let c0 = bytes[0];
    let c1 = bytes[1];
    let is_letter = (c0 >= 0x41 && c0 <= 0x5A) || (c0 >= 0x61 && c0 <= 0x7A);
    if is_letter && c1 == COLON_BYTE {
      if len >= 3 && is_sep_byte(bytes[2]) { return 3; } // C:\
      return 2;                                           // C:foo
    }
  }

  // Rooted but no drive: \foo or /foo
  if is_sep_byte(bytes[0]) { return 1; }
  return 0;
}

/// True iff the path is fully-qualified: starts with drive + root, or UNC.
function is_absolute(path: ref<str>): bool {
  let bytes = path.as_bytes();
  if is_unc(path) { return true; }
  if bytes.len() >= 3 {
    let c0 = bytes[0];
    let c1 = bytes[1];
    let c2 = bytes[2];
    let is_letter = (c0 >= 0x41 && c0 <= 0x5A) || (c0 >= 0x61 && c0 <= 0x7A);
    if is_letter && c1 == COLON_BYTE && is_sep_byte(c2) { return true; }
  }
  return false;
}

function is_relative(path: ref<str>): bool {
  return !is_absolute(path);
}

function has_trailing_sep(path: ref<str>): bool {
  let bytes = path.as_bytes();
  return bytes.len() > 0 && is_sep_byte(bytes[bytes.len() - 1]);
}

/// Return the last component of a path.
function basename(path: ref<str>): own<String> {
  let bytes = path.as_bytes();
  let len = bytes.len();
  if len == 0 { return String::from(""); }

  let start = prefix_len(path);

  // Strip trailing separators down to the prefix.
  let end = len;
  while end > start && is_sep_byte(bytes[end - 1]) {
    end = end - 1;
  }
  if end == start { return String::from(""); }

  // Find last separator after the prefix.
  let i = end;
  while i > start {
    i = i - 1;
    if is_sep_byte(bytes[i]) {
      return String::from(path.slice(i + 1, end));
    }
  }
  return String::from(path.slice(start, end));
}

/// Return the directory part of a path.
function dirname(path: ref<str>): own<String> {
  let bytes = path.as_bytes();
  let len = bytes.len();
  if len == 0 { return String::from("."); }

  let prefix = prefix_len(path);

  // Strip trailing separators but keep the prefix intact.
  let end = len;
  while end > prefix && is_sep_byte(bytes[end - 1]) {
    end = end - 1;
  }

  // Find last separator within the non-prefix portion.
  let i = end;
  while i > prefix {
    i = i - 1;
    if is_sep_byte(bytes[i]) {
      let dir_end = i;
      while dir_end > prefix && is_sep_byte(bytes[dir_end - 1]) {
        dir_end = dir_end - 1;
      }
      // If stripping collapsed back into the prefix, emit the prefix with
      // its trailing separator re-attached when it had one.
      if dir_end == prefix {
        if prefix > 0 && is_sep_byte(bytes[prefix - 1]) {
          return String::from(path.slice(0, prefix));
        }
        // Drive-relative like `C:foo` → dirname `C:`
        return String::from(path.slice(0, prefix));
      }
      return String::from(path.slice(0, dir_end));
    }
  }

  if prefix > 0 {
    return String::from(path.slice(0, prefix));
  }
  return String::from(".");
}

/// Return the extension (including the leading dot) or "" for no extension.
function extname(path: ref<str>): own<String> {
  let base = basename(path);
  let bytes = base.as_str().as_bytes();
  let len = bytes.len();
  let i = len;
  while i > 0 {
    i = i - 1;
    if bytes[i] == DOT_BYTE {
      if i == 0 { return String::from(""); } // dotfile, no extension
      return String::from(base.as_str().slice(i, len));
    }
  }
  return String::from("");
}

/// Basename without extension.
function stem(path: ref<str>): own<String> {
  let base = basename(path);
  let bytes = base.as_str().as_bytes();
  let len = bytes.len();
  let i = len;
  while i > 0 {
    i = i - 1;
    if bytes[i] == DOT_BYTE {
      if i == 0 { return base; }
      return String::from(base.as_str().slice(0, i));
    }
  }
  return base;
}

/// Split a path into (prefix, segments). The prefix preserves its original
/// separator form; segments are the `/` or `\` delimited components after it.
function split(path: ref<str>): own<Vec<own<String>>> {
  let bytes = path.as_bytes();
  let len = bytes.len();
  let result: own<Vec<own<String>>> = Vec::new();

  let prefix = prefix_len(path);
  if prefix > 0 {
    result.push(String::from(path.slice(0, prefix)));
  }

  let start = prefix;
  // Skip separators immediately after the prefix (e.g. `C:\foo` → skip `\`
  // already consumed by prefix_len when it included the root).
  while start < len && is_sep_byte(bytes[start]) { start = start + 1; }

  let i = start;
  while i < len {
    if is_sep_byte(bytes[i]) {
      if i > start {
        result.push(String::from(path.slice(start, i)));
      }
      start = i + 1;
    }
    i = i + 1;
  }
  if start < len {
    result.push(String::from(path.slice(start, len)));
  }
  return result;
}

/// Normalize a path: resolve `.`, `..`, canonicalise separators to `\`,
/// collapse runs of separators. Preserves drive and UNC prefixes.
function normalize(path: ref<str>): own<String> {
  let bytes = path.as_bytes();
  let len = bytes.len();
  if len == 0 { return String::from("."); }

  let prefix = prefix_len(path);
  let prefix_has_root = prefix > 0 && is_sep_byte(bytes[prefix - 1]);

  // Build canonical prefix with backslashes.
  let canonical_prefix = String::new();
  let p: usize = 0;
  while p < prefix {
    if is_sep_byte(bytes[p]) {
      canonical_prefix.push(BACKSLASH);
    } else {
      canonical_prefix.push(bytes[p] as char);
    }
    p = p + 1;
  }

  let segments: own<Vec<own<String>>> = Vec::new();
  let start = prefix;
  while start < len && is_sep_byte(bytes[start]) { start = start + 1; }

  let i = start;
  while i <= len {
    if i == len || is_sep_byte(bytes[i]) {
      if i > start {
        let seg = path.slice(start, i);
        if seg == ".." {
          if segments.len() > 0 && segments.last().unwrap().as_str() != ".." {
            segments.pop();
          } else if !prefix_has_root {
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

  let result = String::from(canonical_prefix.as_str());
  let j: usize = 0;
  while j < segments.len() {
    // Emit a separator before the segment unless prefix already ends with one
    // or we're writing the first segment after a non-rooted prefix.
    if j == 0 {
      if result.len() > 0 && !prefix_has_root {
        // Drive-relative: `C:` + `foo` → `C:foo`
      } else if prefix_has_root {
        // prefix already carries the root separator
      }
    } else {
      result.push(BACKSLASH);
    }
    result.push_str(segments.get(j).unwrap().as_str());
    j = j + 1;
  }

  if result.is_empty() { return String::from("."); }
  return result;
}

/// Join a list of path parts using `\`. An absolute part or a part carrying
/// a drive letter resets the accumulator.
function join(parts: ref<[ref<str>]>): own<String> {
  let result = String::new();
  let i: usize = 0;
  while i < parts.len() {
    let part = parts[i];
    if part.len() == 0 {
      i = i + 1;
      continue;
    }
    if is_absolute(part) {
      // Absolute component wins outright.
      result.clear();
      result.push_str(part);
    } else if drive_letter(part).is_some() {
      // Drive-relative (`C:foo`) also resets.
      result.clear();
      result.push_str(part);
    } else {
      if result.len() > 0 {
        let last = result.as_str().as_bytes()[result.len() - 1];
        if !is_sep_byte(last) {
          result.push(BACKSLASH);
        }
      }
      result.push_str(part);
    }
    i = i + 1;
  }
  if result.is_empty() { return String::from(""); }
  return normalize(result.as_str());
}

/// Resolve a sequence of path segments into a single absolute-style path.
/// Right-to-left scan: the first absolute or drive-rooted segment anchors.
function resolve(parts: ref<[ref<str>]>): own<String> {
  let result = String::new();
  let i = parts.len();
  while i > 0 {
    i = i - 1;
    let part = parts[i];
    if part.len() == 0 { continue; }
    if result.len() > 0 {
      let tmp = String::from(part);
      let last = tmp.as_str().as_bytes()[tmp.len() - 1];
      if !is_sep_byte(last) {
        tmp.push(BACKSLASH);
      }
      tmp.push_str(result.as_str());
      result = tmp;
    } else {
      result = String::from(part);
    }
    if is_absolute(part) || drive_letter(part).is_some() {
      return normalize(result.as_str());
    }
  }
  // Nothing absolute — anchor at `C:\` as a conventional default.
  let abs = String::from("C:\\");
  if result.len() > 0 {
    abs.push_str(result.as_str());
  }
  return normalize(abs.as_str());
}

/// Compute the relative path from `from` to `to`, case-insensitively.
function relative(from: ref<str>, to: ref<str>): own<String> {
  let norm_from = normalize(from);
  let norm_to = normalize(to);

  let from_parts = split(norm_from.as_str());
  let to_parts = split(norm_to.as_str());

  let common: usize = 0;
  let max = from_parts.len();
  if to_parts.len() < max { max = to_parts.len(); }
  while common < max {
    let a = from_parts.get(common).unwrap().as_str();
    let b = to_parts.get(common).unwrap().as_str();
    if !eq_ignore_case(a, b) { break; }
    common = common + 1;
  }

  let result = String::new();
  let ups = from_parts.len() - common;
  let u: usize = 0;
  while u < ups {
    if u > 0 { result.push(BACKSLASH); }
    result.push_str("..");
    u = u + 1;
  }
  let t = common;
  while t < to_parts.len() {
    if result.len() > 0 { result.push(BACKSLASH); }
    result.push_str(to_parts.get(t).unwrap().as_str());
    t = t + 1;
  }
  if result.is_empty() { return String::from("."); }
  return result;
}

/// Replace the extension; `ext` should include the leading dot (or be "").
function with_extname(path: ref<str>, ext: ref<str>): own<String> {
  let dir = dirname(path);
  let base = basename(path);
  let old_ext = extname(base.as_str());
  let stem_len = base.len() - old_ext.len();
  let stem_s = base.as_str().slice(0, stem_len);

  let result = String::new();
  if dir.as_str() != "." {
    result.push_str(dir.as_str());
    let b = result.as_str().as_bytes();
    if result.len() > 0 && !is_sep_byte(b[result.len() - 1]) {
      result.push(BACKSLASH);
    }
  }
  result.push_str(stem_s);
  result.push_str(ext);
  return result;
}

/// Replace the basename component.
function with_basename(path: ref<str>, name: ref<str>): own<String> {
  let dir = dirname(path);
  let result = String::new();
  if dir.as_str() != "." {
    result.push_str(dir.as_str());
    let b = result.as_str().as_bytes();
    if result.len() > 0 && !is_sep_byte(b[result.len() - 1]) {
      result.push(BACKSLASH);
    }
  }
  result.push_str(name);
  return result;
}

/// Case-insensitive ASCII equality. Windows filenames are case-insensitive
/// for the Latin alphabet; this helper does not attempt Unicode folding.
function eq_ignore_case(a: ref<str>, b: ref<str>): bool {
  if a.len() != b.len() { return false; }
  let ab = a.as_bytes();
  let bb = b.as_bytes();
  let i: usize = 0;
  while i < ab.len() {
    if ascii_upper(ab[i]) != ascii_upper(bb[i]) { return false; }
    i = i + 1;
  }
  return true;
}

/// True if `name` (case-insensitive, without extension) is a DOS reserved
/// device name: CON, PRN, AUX, NUL, COM1-9, LPT1-9.
function is_reserved_name(name: ref<str>): bool {
  // Strip any extension first.
  let base = String::from(name);
  let stem_s = stem(base.as_str());
  let s = stem_s.as_str();

  if eq_ignore_case(s, "CON") { return true; }
  if eq_ignore_case(s, "PRN") { return true; }
  if eq_ignore_case(s, "AUX") { return true; }
  if eq_ignore_case(s, "NUL") { return true; }

  // COM1-9 and LPT1-9.
  let bytes = s.as_bytes();
  if bytes.len() == 4 {
    let b3 = bytes[3];
    if b3 >= 0x31 && b3 <= 0x39 {
      let head = s.slice(0, 3);
      if eq_ignore_case(head, "COM") { return true; }
      if eq_ignore_case(head, "LPT") { return true; }
    }
  }
  return false;
}
