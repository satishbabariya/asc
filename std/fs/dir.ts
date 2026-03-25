// std/fs/dir.ts — Directory reading and metadata (RFC-0014)

/// Reads the contents of a directory, returning an iterator of DirEntry.
fn read_dir(path: ref<str>): Result<own<DirIter>, IoError> {
  // Open the directory using WASI path_open with directory flag.
  const dirfd: i32 = 3;  // preopened directory
  let fd: i32 = 0;
  @extern("path_open")
  const errno = wasi_path_open(dirfd, 0, path.as_ptr(), path.len(),
    0x0010,  // OFLAGS_DIRECTORY
    0x4000,  // RIGHT_FD_READDIR
    0, 0, &mut fd);
  if errno != 0 {
    return Result::Err(IoError::from_wasi_errno(errno));
  }

  const buf_size: usize = 4096;
  const buf = malloc(buf_size) as *mut u8;

  return Result::Ok(DirIter {
    fd: fd,
    buf: buf,
    buf_size: buf_size,
    buf_used: 0,
    buf_offset: 0,
    cookie: 0,
    finished: false,
    base_path: String::from(path),
  });
}

/// Iterator over directory entries.
struct DirIter {
  fd: i32,
  buf: *mut u8,
  buf_size: usize,
  buf_used: usize,
  buf_offset: usize,
  cookie: u64,
  finished: bool,
  base_path: own<String>,
}

impl Iterator for DirIter {
  type Item = Result<own<DirEntry>, IoError>;

  fn next(refmut<Self>): Option<Result<own<DirEntry>, IoError>> {
    if self.finished { return Option::None; }

    // Refill buffer if needed.
    if self.buf_offset >= self.buf_used {
      let bytes_used: usize = 0;
      @extern("fd_readdir")
      const errno = wasi_fd_readdir(self.fd, self.buf, self.buf_size,
        self.cookie, &mut bytes_used);
      if errno != 0 {
        return Option::Some(Result::Err(IoError::from_wasi_errno(errno)));
      }
      if bytes_used == 0 {
        self.finished = true;
        return Option::None;
      }
      self.buf_used = bytes_used;
      self.buf_offset = 0;
    }

    // Parse the WASI dirent structure at buf_offset.
    // Layout: d_next(u64) + d_ino(u64) + d_namlen(u32) + d_type(u8)
    const entry_ptr = (self.buf as usize + self.buf_offset) as *const u8;
    const d_next = unsafe { *(entry_ptr as *const u64) };
    const d_ino = unsafe { *((entry_ptr as usize + 8) as *const u64) };
    const d_namlen = unsafe { *((entry_ptr as usize + 16) as *const u32) };
    const d_type = unsafe { *((entry_ptr as usize + 20) as *const u8) };

    const name_ptr = (entry_ptr as usize + 24) as *const u8;
    const name_len = d_namlen as usize;

    // Copy the name into a String.
    let name = String::with_capacity(name_len);
    let i: usize = 0;
    while i < name_len {
      name.push(unsafe { *((name_ptr as usize + i) as *const u8) } as char);
      i = i + 1;
    }

    // Build the full path.
    const base_path = Path::from_str(self.base_path.as_str());
    const full_path = base_path.join(name.as_str());

    self.cookie = d_next;
    self.buf_offset = self.buf_offset + 24 + name_len;

    // Determine file type.
    const file_type = match d_type {
      4 => FileType::Directory,
      6 => FileType::RegularFile,
      7 => FileType::SymbolicLink,
      _ => FileType::Other,
    };

    return Option::Some(Result::Ok(DirEntry {
      name: name,
      path: full_path,
      file_type: file_type,
      ino: d_ino,
    }));
  }
}

impl Drop for DirIter {
  fn drop(refmut<Self>): void {
    if self.buf != null { free(self.buf); }
    @extern("fd_close")
    wasi_fd_close(self.fd);
  }
}

/// A single entry in a directory listing.
struct DirEntry {
  name: own<String>,
  path: own<Path>,
  file_type: FileType,
  ino: u64,
}

impl DirEntry {
  /// Returns the full path of this entry.
  fn path(ref<Self>): ref<Path> {
    return &self.path;
  }

  /// Returns just the file name of this entry.
  fn file_name(ref<Self>): ref<str> {
    return self.name.as_str();
  }

  /// Returns the file type of this entry.
  fn file_type(ref<Self>): Result<FileType, IoError> {
    return Result::Ok(self.file_type);
  }

  /// Returns full metadata for this entry (may require additional syscall).
  fn metadata(ref<Self>): Result<Metadata, IoError> {
    return fs::metadata(self.path.as_str());
  }
}

/// Type of a filesystem entry. @copy.
enum FileType {
  RegularFile,
  Directory,
  SymbolicLink,
  Other,
}

impl FileType {
  fn is_file(ref<Self>): bool {
    match self { FileType::RegularFile => true, _ => false }
  }

  fn is_dir(ref<Self>): bool {
    match self { FileType::Directory => true, _ => false }
  }

  fn is_symlink(ref<Self>): bool {
    match self { FileType::SymbolicLink => true, _ => false }
  }
}

/// Metadata about a filesystem entry. @copy.
struct Metadata {
  file_type: u8,
  size: u64,
  atim: u64,
  mtim: u64,
  ctim: u64,
}

impl Metadata {
  fn is_file(ref<Self>): bool { return self.file_type == 6; }
  fn is_dir(ref<Self>): bool { return self.file_type == 4; }
  fn is_symlink(ref<Self>): bool { return self.file_type == 7; }
  fn len(ref<Self>): u64 { return self.size; }
}
