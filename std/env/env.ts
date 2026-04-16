// std/env/env.ts — Environment variable access (RFC-0019)

/// External: get an environment variable value.
/// Returns 0 on success, non-zero if not found.
@extern("env", "__asc_env_get")
declare function __asc_env_get(key_ptr: *const u8, key_len: usize, buf_ptr: *mut u8, buf_cap: usize, out_len: *mut usize): i32;

/// External: set an environment variable.
@extern("env", "__asc_env_set")
declare function __asc_env_set(key_ptr: *const u8, key_len: usize, val_ptr: *const u8, val_len: usize): i32;

/// External: get the count of environment variables.
@extern("env", "__asc_env_count")
declare function __asc_env_count(): usize;

/// External: get an environment variable by index.
@extern("env", "__asc_env_get_nth")
declare function __asc_env_get_nth(
  index: usize,
  key_buf: *mut u8, key_cap: usize, key_len: *mut usize,
  val_buf: *mut u8, val_cap: usize, val_len: *mut usize,
): i32;

/// Get the value of an environment variable.
function get(key: ref<str>): Option<own<String>> {
  let buf: [u8; 4096] = [0u8; 4096];
  let out_len: usize = 0;
  let rc = __asc_env_get(key.as_ptr(), key.len(), buf.as_mut_ptr(), 4096, &mut out_len);
  if rc != 0 { return Option::None; }
  return Option::Some(String::from(unsafe { str::from_raw_parts(buf.as_ptr(), out_len) }));
}

/// Get an environment variable or return a default value.
function get_or(key: ref<str>, default_value: own<String>): own<String> {
  match get(key) {
    Option::Some(val) => { return val; },
    Option::None => { return default_value; },
  }
}

/// Set an environment variable.
function set(key: ref<str>, value: ref<str>): void {
  __asc_env_set(key.as_ptr(), key.len(), value.as_ptr(), value.len());
}

/// Remove an environment variable (set to empty).
function remove(key: ref<str>): void {
  __asc_env_set(key.as_ptr(), key.len(), null, 0);
}

/// Entry for iterating environment variables.
struct EnvEntry {
  key: own<String>,
  value: own<String>,
}

/// Get all environment variables as a vector of (key, value) pairs.
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

/// Get the current working directory.
@extern("env", "__asc_getcwd")
declare function __asc_getcwd(buf: *mut u8, cap: usize, out_len: *mut usize): i32;

function cwd(): Option<own<String>> {
  let buf: [u8; 4096] = [0u8; 4096];
  let out_len: usize = 0;
  let rc = __asc_getcwd(buf.as_mut_ptr(), 4096, &mut out_len);
  if rc != 0 { return Option::None; }
  return Option::Some(String::from(unsafe { str::from_raw_parts(buf.as_ptr(), out_len) }));
}
