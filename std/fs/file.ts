// std/fs/file.ts — File: WASI file handle with Read/Write/Seek (RFC-0014)

/// An owned file descriptor. RAII: closes on drop.
struct File {
  fd: i32,
}

impl File {
  /// Opens a file for reading.
  fn open(path: ref<str>): Result<own<File>, IoError> {
    return OpenOptions::new().read(true).open(path);
  }

  /// Creates a file for writing, truncating if it exists.
  fn create(path: ref<str>): Result<own<File>, IoError> {
    return OpenOptions::new().write(true).create(true).truncate(true).open(path);
  }

  /// Returns an OpenOptions builder for fine-grained control.
  fn open_options(): OpenOptions {
    return OpenOptions::new();
  }

  /// Returns metadata about the file.
  fn metadata(ref<Self>): Result<Metadata, IoError> {
    let stat: WasiFilestat;
    @extern("fd_filestat_get")
    const errno = wasi_fd_filestat_get(self.fd, &mut stat);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(Metadata {
      file_type: stat.filetype,
      size: stat.size,
      atim: stat.atim,
      mtim: stat.mtim,
      ctim: stat.ctim,
    });
  }

  /// Truncates or extends the file to the specified size.
  fn set_len(ref<Self>, size: u64): Result<void, IoError> {
    @extern("fd_filestat_set_size")
    const errno = wasi_fd_filestat_set_size(self.fd, size);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(());
  }

  /// Synchronizes all data and metadata to disk.
  fn sync_all(ref<Self>): Result<void, IoError> {
    @extern("fd_sync")
    const errno = wasi_fd_sync(self.fd);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(());
  }

  /// Returns the raw WASI file descriptor.
  fn as_raw_fd(ref<Self>): i32 {
    return self.fd;
  }
}

impl Read for File {
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

impl Write for File {
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
    @extern("fd_datasync")
    const errno = wasi_fd_datasync(self.fd);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(());
  }
}

impl Seek for File {
  fn seek(refmut<Self>, pos: SeekFrom): Result<u64, IoError> {
    let offset: i64;
    let whence: u8;
    match pos {
      SeekFrom::Start(n) => { offset = n as i64; whence = 0; },
      SeekFrom::Current(n) => { offset = n; whence = 1; },
      SeekFrom::End(n) => { offset = n; whence = 2; },
    }
    let new_offset: u64 = 0;
    @extern("fd_seek")
    const errno = wasi_fd_seek(self.fd, offset, whence, &mut new_offset);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(new_offset);
  }
}

impl Drop for File {
  fn drop(refmut<Self>): void {
    @extern("fd_close")
    wasi_fd_close(self.fd);
  }
}

// ---------- OpenOptions ----------

/// Builder for configuring how a file is opened. @copy.
struct OpenOptions {
  _read: bool,
  _write: bool,
  _append: bool,
  _truncate: bool,
  _create: bool,
  _create_new: bool,
}

impl OpenOptions {
  fn new(): OpenOptions {
    return OpenOptions {
      _read: false, _write: false, _append: false,
      _truncate: false, _create: false, _create_new: false,
    };
  }

  fn read(self: OpenOptions, v: bool): OpenOptions {
    return OpenOptions { _read: v, _write: self._write, _append: self._append,
      _truncate: self._truncate, _create: self._create, _create_new: self._create_new };
  }

  fn write(self: OpenOptions, v: bool): OpenOptions {
    return OpenOptions { _read: self._read, _write: v, _append: self._append,
      _truncate: self._truncate, _create: self._create, _create_new: self._create_new };
  }

  fn append(self: OpenOptions, v: bool): OpenOptions {
    return OpenOptions { _read: self._read, _write: self._write, _append: v,
      _truncate: self._truncate, _create: self._create, _create_new: self._create_new };
  }

  fn truncate(self: OpenOptions, v: bool): OpenOptions {
    return OpenOptions { _read: self._read, _write: self._write, _append: self._append,
      _truncate: v, _create: self._create, _create_new: self._create_new };
  }

  fn create(self: OpenOptions, v: bool): OpenOptions {
    return OpenOptions { _read: self._read, _write: self._write, _append: self._append,
      _truncate: self._truncate, _create: v, _create_new: self._create_new };
  }

  fn create_new(self: OpenOptions, v: bool): OpenOptions {
    return OpenOptions { _read: self._read, _write: self._write, _append: self._append,
      _truncate: self._truncate, _create: self._create, _create_new: v };
  }

  fn open(self: OpenOptions, path: ref<str>): Result<own<File>, IoError> {
    // Compute WASI oflags and rights.
    let oflags: u16 = 0;
    let rights: u64 = 0;
    let fdflags: u16 = 0;

    if self._create { oflags = oflags | 0x0001; }      // OFLAGS_CREAT
    if self._create_new { oflags = oflags | 0x0005; }   // OFLAGS_CREAT | OFLAGS_EXCL
    if self._truncate { oflags = oflags | 0x0008; }     // OFLAGS_TRUNC

    if self._read { rights = rights | 0x0002; }         // RIGHT_FD_READ
    if self._write { rights = rights | 0x0040; }        // RIGHT_FD_WRITE
    if self._append { fdflags = fdflags | 0x0001; }     // FDFLAGS_APPEND

    // Use preopened directory fd 3 as the base directory.
    const dirfd: i32 = 3;
    let fd: i32 = 0;
    @extern("path_open")
    const errno = wasi_path_open(dirfd, 0, path.as_ptr(), path.len(),
      oflags, rights, rights, fdflags, &mut fd);
    if errno != 0 {
      return Result::Err(IoError::from_wasi_errno(errno));
    }
    return Result::Ok(File { fd: fd });
  }
}

// ---------- WASI structures ----------

struct WasiIovec { buf: *mut u8, len: usize }
struct WasiCiovec { buf: *const u8, len: usize }
struct WasiFilestat {
  dev: u64,
  ino: u64,
  filetype: u8,
  nlink: u64,
  size: u64,
  atim: u64,
  mtim: u64,
  ctim: u64,
}
