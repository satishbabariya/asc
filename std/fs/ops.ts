// std/fs/ops.ts — Filesystem convenience functions (RFC-0014)

/// Reads the entire contents of a file as a string.
fn read_to_string(path: ref<str>): Result<own<String>, IoError> {
  let file = File::open(path)?;
  let content = String::new();
  let buf: [u8; 4096];
  loop {
    const n = file.read(&mut buf)?;
    if n == 0 { break; }
    let i: usize = 0;
    while i < n {
      content.push(buf[i] as char);
      i = i + 1;
    }
  }
  return Result::Ok(content);
}

/// Reads the entire contents of a file as bytes.
fn read(path: ref<str>): Result<own<Vec<u8>>, IoError> {
  let file = File::open(path)?;
  let bytes = Vec::new();
  let buf: [u8; 4096];
  loop {
    const n = file.read(&mut buf)?;
    if n == 0 { break; }
    let i: usize = 0;
    while i < n {
      bytes.push(buf[i]);
      i = i + 1;
    }
  }
  return Result::Ok(bytes);
}

/// Writes the given bytes to a file, creating or truncating it.
fn write(path: ref<str>, contents: ref<[u8]>): Result<void, IoError> {
  let file = File::create(path)?;
  file.write_all(contents)?;
  return Result::Ok(());
}

/// Copies the contents of one file to another. Returns the number of bytes copied.
fn copy(from: ref<str>, to: ref<str>): Result<u64, IoError> {
  let src = File::open(from)?;
  let dst = File::create(to)?;
  let total: u64 = 0;
  let buf: [u8; 8192];
  loop {
    const n = src.read(&mut buf)?;
    if n == 0 { break; }
    const data = unsafe { slice::from_raw_parts(&buf[0], n) };
    dst.write_all(data)?;
    total = total + n as u64;
  }
  return Result::Ok(total);
}

/// Renames a file or directory.
fn rename(from: ref<str>, to: ref<str>): Result<void, IoError> {
  const dirfd: i32 = 3;  // preopened directory
  @extern("path_rename")
  const errno = wasi_path_rename(dirfd, from.as_ptr(), from.len(),
    dirfd, to.as_ptr(), to.len());
  if errno != 0 {
    return Result::Err(IoError::from_wasi_errno(errno));
  }
  return Result::Ok(());
}

/// Removes a file.
fn remove_file(path: ref<str>): Result<void, IoError> {
  const dirfd: i32 = 3;
  @extern("path_unlink_file")
  const errno = wasi_path_unlink_file(dirfd, path.as_ptr(), path.len());
  if errno != 0 {
    return Result::Err(IoError::from_wasi_errno(errno));
  }
  return Result::Ok(());
}

/// Creates a directory.
fn create_dir(path: ref<str>): Result<void, IoError> {
  const dirfd: i32 = 3;
  @extern("path_create_directory")
  const errno = wasi_path_create_directory(dirfd, path.as_ptr(), path.len());
  if errno != 0 {
    return Result::Err(IoError::from_wasi_errno(errno));
  }
  return Result::Ok(());
}

/// Creates a directory and all its parent directories.
fn create_dir_all(path: ref<str>): Result<void, IoError> {
  const result = create_dir(path);
  match result {
    Result::Ok(_) => { return Result::Ok(()); },
    Result::Err(e) => {
      match e.kind() {
        ErrorKind::AlreadyExists => { return Result::Ok(()); },
        ErrorKind::NotFound => {
          const p = Path::from_str(path);
          match p.parent() {
            Option::Some(parent) => {
              create_dir_all(parent.as_str())?;
              return create_dir(path);
            },
            Option::None => { return Result::Err(e); },
          }
        },
        _ => { return Result::Err(e); },
      }
    },
  }
}

/// Removes an empty directory.
fn remove_dir(path: ref<str>): Result<void, IoError> {
  const dirfd: i32 = 3;
  @extern("path_remove_directory")
  const errno = wasi_path_remove_directory(dirfd, path.as_ptr(), path.len());
  if errno != 0 {
    return Result::Err(IoError::from_wasi_errno(errno));
  }
  return Result::Ok(());
}

/// Returns metadata for a path.
fn metadata(path: ref<str>): Result<Metadata, IoError> {
  const dirfd: i32 = 3;
  let stat: WasiFilestat;
  @extern("path_filestat_get")
  const errno = wasi_path_filestat_get(dirfd, 0, path.as_ptr(), path.len(), &mut stat);
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
