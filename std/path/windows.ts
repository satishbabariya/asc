// std/path/windows.ts — Windows path manipulation (RFC-0019)
//
// DECISION: Windows path support is a thin wrapper that adjusts
// separators from / to \ and handles drive letters.

/// Convert forward slashes to backslashes.
function to_windows(path: ref<str>): own<String> {
  let result = String::with_capacity(path.len());
  let i: usize = 0;
  while i < path.len() {
    const c = path.char_at(i);
    if c == '/' { result.push('\\'); }
    else { result.push(c); }
    i = i + 1;
  }
  return result;
}

/// Convert backslashes to forward slashes.
function to_posix(path: ref<str>): own<String> {
  let result = String::with_capacity(path.len());
  let i: usize = 0;
  while i < path.len() {
    const c = path.char_at(i);
    if c == '\\' { result.push('/'); }
    else { result.push(c); }
    i = i + 1;
  }
  return result;
}

/// Check if a Windows path is absolute (starts with drive letter or UNC).
function is_absolute(path: ref<str>): bool {
  if path.len() >= 3 {
    const c0 = path.char_at(0);
    const c1 = path.char_at(1);
    const c2 = path.char_at(2);
    // Drive letter: C:\
    if ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'))
        && c1 == ':' && (c2 == '\\' || c2 == '/') {
      return true;
    }
  }
  // UNC: \\server
  if path.len() >= 2 {
    if path.starts_with("\\\\") || path.starts_with("//") {
      return true;
    }
  }
  return false;
}

/// Get the drive letter from a Windows path, or None.
function drive_letter(path: ref<str>): Option<char> {
  if path.len() >= 2 {
    const c0 = path.char_at(0);
    const c1 = path.char_at(1);
    if ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z')) && c1 == ':' {
      return Option::Some(c0);
    }
  }
  return Option::None;
}
