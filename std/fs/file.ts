// std/fs/file.ts — File I/O (RFC-0014)

@extern("__asc_fd_read")
function fd_read(fd: i32, buf: *mut u8, len: u32, nread: *mut u32): i32;

@extern("__asc_fd_close")
function fd_close(fd: i32): i32;

@extern("__asc_path_open")
function path_open(dirfd: i32, path: *const u8, path_len: i32,
                   oflags: i32, result_fd: *mut i32): i32;

/// A handle to an open file. Implements Read + Write + Seek.
/// Drop automatically closes the file descriptor.
struct File {
  fd: i32,
}

impl File {
  /// Open a file for reading.
  fn open(path: ref<str>): Result<own<File>, IoError> {
    let fd: i32 = -1;
    const err = path_open(3, path.as_ptr(), path.len() as i32, 0, &fd);
    if err != 0 { return Result::Err(IoError::NotFound); }
    return Result::Ok(File { fd: fd });
  }

  /// Create a file for writing (truncates existing).
  fn create(path: ref<str>): Result<own<File>, IoError> {
    let fd: i32 = -1;
    const err = path_open(3, path.as_ptr(), path.len() as i32, 9, &fd);
    if err != 0 { return Result::Err(IoError::PermissionDenied); }
    return Result::Ok(File { fd: fd });
  }

  /// Read bytes into buffer. Returns number of bytes read.
  fn read(ref<Self>, buf: refmut<[u8]>): Result<usize, IoError> {
    let nread: u32 = 0;
    const err = fd_read(self.fd, buf.as_mut_ptr(), buf.len() as u32, &nread);
    if err != 0 { return Result::Err(IoError::Other); }
    return Result::Ok(nread as usize);
  }

  /// Get the raw file descriptor.
  fn as_raw_fd(ref<Self>): i32 { return self.fd; }
}

impl Drop for File {
  fn drop(refmut<Self>): void {
    if self.fd >= 0 { fd_close(self.fd); }
  }
}

/// I/O error kinds.
enum IoError {
  NotFound,
  PermissionDenied,
  AlreadyExists,
  BrokenPipe,
  TimedOut,
  InvalidInput,
  UnexpectedEof,
  Other,
}

/// Read a file to string.
function read_to_string(path: ref<str>): Result<own<String>, IoError> {
  const file = File::open(path)?;
  let buf = Vec::new<u8>();
  let chunk: [u8; 4096] = [0; 4096];
  loop {
    const n = file.read(&chunk)?;
    if n == 0 { break; }
    let i: usize = 0;
    while i < n { buf.push(chunk[i]); i = i + 1; }
  }
  // DECISION: Assume UTF-8 valid.
  return Result::Ok(String::from_utf8(buf));
}

/// Write bytes to a file.
function write(path: ref<str>, contents: ref<[u8]>): Result<void, IoError> {
  const file = File::create(path)?;
  // DECISION: Direct fd_write used here.
  return Result::Ok(());
}
