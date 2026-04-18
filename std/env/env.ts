// std/env/env.ts — Environment variable access (RFC-0019 §2.1)

/// Runtime bindings — one set for Wasm (WASI environ_get) and one for
/// native (POSIX getenv/setenv). See lib/Runtime/wasi_env.c for the
/// implementations. All functions work on borrowed byte slices so no
/// allocator coupling is required at the FFI boundary.

@extern("env", "__asc_env_get")
declare function __asc_env_get(key_ptr: *const u8, key_len: usize, buf_ptr: *mut u8, buf_cap: usize, out_len: *mut usize): i32;

@extern("env", "__asc_env_set")
declare function __asc_env_set(key_ptr: *const u8, key_len: usize, val_ptr: *const u8, val_len: usize): i32;

@extern("env", "__asc_env_remove")
declare function __asc_env_remove(key_ptr: *const u8, key_len: usize): i32;

@extern("env", "__asc_env_count")
declare function __asc_env_count(): usize;

@extern("env", "__asc_env_get_nth")
declare function __asc_env_get_nth(index: usize, key_buf: *mut u8, key_cap: usize, key_len: *mut usize, val_buf: *mut u8, val_cap: usize, val_len: *mut usize): i32;

@extern("env", "__asc_getcwd")
declare function __asc_getcwd(buf: *mut u8, cap: usize, out_len: *mut usize): i32;

@extern("env", "__asc_exe_path")
declare function __asc_exe_path(buf: *mut u8, cap: usize, out_len: *mut usize): i32;

/// Error type for env access per RFC-0019 §2.3.
enum EnvError {
  NotSet(own<String>),
  ParseError(own<String>, own<String>, own<String>),
  InvalidFormat(usize, own<String>),
  IoError(own<String>),
  NotSupported(own<String>),
}

// ── §2.1 Accessors ─────────────────────────────────────────────────

/// Get an environment variable. Returns None if not set.
function var(name: ref<str>): Option<own<String>> {
  let buf: [u8; 4096] = [0u8; 4096];
  let out_len: usize = 0;
  let rc = __asc_env_get(name.as_ptr(), name.len(),
                          buf.as_mut_ptr(), 4096, &mut out_len);
  if rc != 0 { return Option::None; }
  return Option::Some(String::from(unsafe {
    str::from_raw_parts(buf.as_ptr(), out_len)
  }));
}

/// Get an environment variable or return a default.
function var_or(name: ref<str>, default_value: own<String>): own<String> {
  match var(name) {
    Option::Some(val) => { return val; },
    Option::None => { return default_value; },
  }
}

// ── Typed parsers (RFC-0019 §2.3, concrete form) ───────────────────

/// Parse an env var as signed 32-bit. Accepts optional leading '+'/'-',
/// decimal digits only. Returns None on missing or malformed input.
function var_i32(name: ref<str>): Option<i32> {
  match var(name) {
    Option::None => { return Option::None; },
    Option::Some(s) => {
      let bytes = s.as_str().as_bytes();
      let len = bytes.len();
      if len == 0 { return Option::None; }
      let i: usize = 0;
      let negative: bool = false;
      if bytes[0] == 0x2D { negative = true; i = 1; }
      else if bytes[0] == 0x2B { i = 1; }
      if i == len { return Option::None; }
      let acc: i64 = 0;
      while i < len {
        let c = bytes[i];
        if c < 0x30 || c > 0x39 { return Option::None; }
        acc = acc * 10 + ((c - 0x30) as i64);
        if acc > 2147483648 { return Option::None; }
        i = i + 1;
      }
      let signed: i64 = if negative { -acc } else { acc };
      if signed > 2147483647 || signed < -2147483648 { return Option::None; }
      return Option::Some(signed as i32);
    },
  }
}

/// Parse an env var as unsigned 64-bit.
function var_u64(name: ref<str>): Option<u64> {
  match var(name) {
    Option::None => { return Option::None; },
    Option::Some(s) => {
      let bytes = s.as_str().as_bytes();
      let len = bytes.len();
      if len == 0 { return Option::None; }
      let i: usize = 0;
      if bytes[0] == 0x2B { i = 1; }
      if i == len { return Option::None; }
      let acc: u64 = 0;
      while i < len {
        let c = bytes[i];
        if c < 0x30 || c > 0x39 { return Option::None; }
        // Overflow check: acc * 10 + digit must fit in u64.
        if acc > 1844674407370955161 { return Option::None; }
        let digit = (c - 0x30) as u64;
        if acc == 1844674407370955161 && digit > 5 { return Option::None; }
        acc = acc * 10 + digit;
        i = i + 1;
      }
      return Option::Some(acc);
    },
  }
}

/// Parse an env var as a boolean. Accepted (case-sensitive):
/// "1","true","TRUE","yes","on" → true; "0","false","FALSE","no","off" → false.
function var_bool(name: ref<str>): Option<bool> {
  match var(name) {
    Option::None => { return Option::None; },
    Option::Some(s) => {
      let v = s.as_str();
      if v.eq("1") || v.eq("true") || v.eq("TRUE") || v.eq("True")
         || v.eq("yes") || v.eq("YES") || v.eq("on") || v.eq("ON") {
        return Option::Some(true);
      }
      if v.eq("0") || v.eq("false") || v.eq("FALSE") || v.eq("False")
         || v.eq("no") || v.eq("NO") || v.eq("off") || v.eq("OFF") {
        return Option::Some(false);
      }
      return Option::None;
    },
  }
}

/// Parse an env var as f64. Minimal parser — accepts integer or decimal
/// form with optional sign and optional fractional part. No exponent,
/// NaN, or inf (keeps the parser small and panic-free).
function var_f64(name: ref<str>): Option<f64> {
  match var(name) {
    Option::None => { return Option::None; },
    Option::Some(s) => {
      let bytes = s.as_str().as_bytes();
      let len = bytes.len();
      if len == 0 { return Option::None; }
      let i: usize = 0;
      let negative: bool = false;
      if bytes[0] == 0x2D { negative = true; i = 1; }
      else if bytes[0] == 0x2B { i = 1; }
      if i == len { return Option::None; }
      let int_part: f64 = 0.0;
      let saw_digit: bool = false;
      while i < len && bytes[i] != 0x2E {
        let c = bytes[i];
        if c < 0x30 || c > 0x39 { return Option::None; }
        int_part = int_part * 10.0 + ((c - 0x30) as f64);
        saw_digit = true;
        i = i + 1;
      }
      let frac_part: f64 = 0.0;
      let scale: f64 = 1.0;
      if i < len && bytes[i] == 0x2E {
        i = i + 1;
        while i < len {
          let c = bytes[i];
          if c < 0x30 || c > 0x39 { return Option::None; }
          frac_part = frac_part * 10.0 + ((c - 0x30) as f64);
          scale = scale * 10.0;
          saw_digit = true;
          i = i + 1;
        }
      }
      if !saw_digit { return Option::None; }
      let magnitude = int_part + frac_part / scale;
      return Option::Some(if negative { -magnitude } else { magnitude });
    },
  }
}

/// Set an environment variable (current process only).
function set_var(name: ref<str>, value: ref<str>): void {
  __asc_env_set(name.as_ptr(), name.len(), value.as_ptr(), value.len());
}

/// Remove an environment variable.
function remove_var(name: ref<str>): void {
  __asc_env_remove(name.as_ptr(), name.len());
}

/// Entry yielded by vars().
struct EnvEntry {
  key: own<String>,
  value: own<String>,
}

/// All environment variables, as a vector of EnvEntry.
/// Returns a snapshot — mutations via set_var/remove_var after calling
/// vars() are not reflected in the returned vector.
function vars(): own<Vec<EnvEntry>> {
  let count = __asc_env_count();
  let result: own<Vec<EnvEntry>> = Vec::with_capacity(count);
  let key_buf: [u8; 4096] = [0u8; 4096];
  let val_buf: [u8; 4096] = [0u8; 4096];

  let i: usize = 0;
  while i < count {
    let key_len: usize = 0;
    let val_len: usize = 0;
    let rc = __asc_env_get_nth(
      i,
      key_buf.as_mut_ptr(), 4096, &mut key_len,
      val_buf.as_mut_ptr(), 4096, &mut val_len,
    );
    if rc == 0 {
      let key = String::from(unsafe { str::from_raw_parts(key_buf.as_ptr(), key_len) });
      let val = String::from(unsafe { str::from_raw_parts(val_buf.as_ptr(), val_len) });
      result.push(EnvEntry { key: key, value: val });
    }
    i = i + 1;
  }
  return result;
}

// ── Process paths ──────────────────────────────────────────────────

/// Current working directory.
/// On Wasm this falls back to the PWD env var, since WASI preview1
/// has no cwd primitive. On native targets this calls getcwd(3).
function cwd(): Result<own<String>, EnvError> {
  let buf: [u8; 4096] = [0u8; 4096];
  let out_len: usize = 0;
  let rc = __asc_getcwd(buf.as_mut_ptr(), 4096, &mut out_len);
  if rc != 0 {
    return Result::Err(EnvError::IoError(String::from("cwd unavailable")));
  }
  return Result::Ok(String::from(unsafe {
    str::from_raw_parts(buf.as_ptr(), out_len)
  }));
}

/// Current executable path.
/// On Wasm this returns argv[0] via WASI args_get (may be a logical name,
/// not an absolute path). On macOS this uses _NSGetExecutablePath; on
/// Linux it reads /proc/self/exe. Returns NotSupported on platforms
/// with no reliable primitive.
function exe(): Result<own<String>, EnvError> {
  let buf: [u8; 4096] = [0u8; 4096];
  let out_len: usize = 0;
  let rc = __asc_exe_path(buf.as_mut_ptr(), 4096, &mut out_len);
  if rc != 0 {
    return Result::Err(EnvError::NotSupported(String::from("exe path unavailable")));
  }
  return Result::Ok(String::from(unsafe {
    str::from_raw_parts(buf.as_ptr(), out_len)
  }));
}

// ── Back-compat shims (kept so existing callers don't break) ───────

/// Alias for `var` — retained for continuity with pre-RFC code.
function get(key: ref<str>): Option<own<String>> { return var(key); }

/// Alias for `var_or`.
function get_or(key: ref<str>, default_value: own<String>): own<String> {
  return var_or(key, default_value);
}

/// Alias for `set_var`.
function set(key: ref<str>, value: ref<str>): void { set_var(key, value); }

/// Alias for `remove_var`.
function remove(key: ref<str>): void { remove_var(key); }
