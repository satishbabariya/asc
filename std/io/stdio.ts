// std/io/stdio.ts — Standard I/O streams via WASI fd 0/1/2 (RFC-0014)

/// Standard input stream (WASI fd 0).
struct Stdin {
  fd: i32,
}

/// Returns a handle to the standard input stream.
fn stdin(): Stdin {
  return Stdin { fd: 0 };
}

impl Read for Stdin {
  fn read(refmut<Self>, buf: refmut<[u8]>): Result<usize, IoError> {
    const iov = WasiIovec { buf: buf.as_mut_ptr(), len: buf.len() };
    let nread: usize = 0;
    @extern("fd_read")
    const errno = wasi_fd_read(self.fd, &iov, 1, &mut nread);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(nread);
  }
}

/// Standard output stream (WASI fd 1).
struct Stdout {
  fd: i32,
}

/// Returns a handle to the standard output stream.
fn stdout(): Stdout {
  return Stdout { fd: 1 };
}

impl Write for Stdout {
  fn write(refmut<Self>, buf: ref<[u8]>): Result<usize, IoError> {
    const ciov = WasiCiovec { buf: buf.as_ptr(), len: buf.len() };
    let nwritten: usize = 0;
    @extern("fd_write")
    const errno = wasi_fd_write(self.fd, &ciov, 1, &mut nwritten);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(nwritten);
  }

  fn flush(refmut<Self>): Result<void, IoError> {
    @extern("fd_sync")
    const errno = wasi_fd_sync(self.fd);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(());
  }
}

/// Standard error stream (WASI fd 2).
struct Stderr {
  fd: i32,
}

/// Returns a handle to the standard error stream.
fn stderr(): Stderr {
  return Stderr { fd: 2 };
}

impl Write for Stderr {
  fn write(refmut<Self>, buf: ref<[u8]>): Result<usize, IoError> {
    const ciov = WasiCiovec { buf: buf.as_ptr(), len: buf.len() };
    let nwritten: usize = 0;
    @extern("fd_write")
    const errno = wasi_fd_write(self.fd, &ciov, 1, &mut nwritten);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(nwritten);
  }

  fn flush(refmut<Self>): Result<void, IoError> {
    // stderr is unbuffered — flush is a no-op.
    return Result::Ok(());
  }
}

// ---------- WASI iovec structures ----------

/// WASI iovec for read operations.
struct WasiIovec {
  buf: *mut u8,
  len: usize,
}

/// WASI ciovec for write operations (const buffer).
struct WasiCiovec {
  buf: *const u8,
  len: usize,
}
