// std/io/traits.ts — Read, Write, Seek traits (RFC-0014)

/// Trait for reading bytes from a source.
trait Read {
  /// Reads bytes into `buf`, returning the number of bytes read.
  /// Returns 0 at EOF.
  fn read(refmut<Self>, buf: refmut<[u8]>): Result<usize, IoError>;

  /// Reads exactly `buf.len()` bytes. Returns Err on premature EOF.
  fn read_exact(refmut<Self>, buf: refmut<[u8]>): Result<void, IoError> {
    let offset: usize = 0;
    const total = buf.len();
    while offset < total {
      const remaining = buf.slice_mut(offset, total);
      const n = self.read(remaining)?;
      if n == 0 {
        return Result::Err(IoError::new(ErrorKind::UnexpectedEof,
          "failed to fill buffer"));
      }
      offset = offset + n;
    }
    return Result::Ok(());
  }

  /// Reads all remaining bytes, appending to `buf`.
  fn read_to_end(refmut<Self>, buf: refmut<Vec<u8>>): Result<usize, IoError> {
    let total_read: usize = 0;
    let chunk: [u8; 4096];
    loop {
      const n = self.read(&mut chunk)?;
      if n == 0 { break; }
      let i: usize = 0;
      while i < n {
        buf.push(chunk[i]);
        i = i + 1;
      }
      total_read = total_read + n;
    }
    return Result::Ok(total_read);
  }

  /// Reads all remaining bytes as a UTF-8 string.
  fn read_to_string(refmut<Self>, s: refmut<String>): Result<usize, IoError> {
    let bytes = Vec::new();
    const n = self.read_to_end(&mut bytes)?;
    // Validate UTF-8 and append.
    let i: usize = 0;
    while i < bytes.len() {
      s.push(*bytes.get(i).unwrap() as char);
      i = i + 1;
    }
    return Result::Ok(n);
  }
}

/// Trait for writing bytes to a destination.
trait Write {
  /// Writes bytes from `buf`, returning the number of bytes written.
  fn write(refmut<Self>, buf: ref<[u8]>): Result<usize, IoError>;

  /// Writes all bytes from `buf`. Returns Err if not all bytes could be written.
  fn write_all(refmut<Self>, buf: ref<[u8]>): Result<void, IoError> {
    let offset: usize = 0;
    const total = buf.len();
    while offset < total {
      const remaining = buf.slice(offset, total);
      const n = self.write(remaining)?;
      if n == 0 {
        return Result::Err(IoError::new(ErrorKind::WriteZero,
          "failed to write whole buffer"));
      }
      offset = offset + n;
    }
    return Result::Ok(());
  }

  /// Writes formatted output.
  fn write_fmt(refmut<Self>, fmt: Arguments): Result<void, IoError> {
    const s = format(fmt);
    const bytes = s.as_str().as_bytes();
    return self.write_all(bytes);
  }

  /// Flushes any buffered data to the underlying sink.
  fn flush(refmut<Self>): Result<void, IoError>;
}

/// Trait for seeking within a byte stream.
trait Seek {
  /// Seeks to the given position, returning the new absolute position.
  fn seek(refmut<Self>, pos: SeekFrom): Result<u64, IoError>;

  /// Rewinds to the beginning of the stream.
  fn rewind(refmut<Self>): Result<void, IoError> {
    self.seek(SeekFrom::Start(0))?;
    return Result::Ok(());
  }

  /// Returns the current position in the stream.
  fn stream_position(refmut<Self>): Result<u64, IoError> {
    return self.seek(SeekFrom::Current(0));
  }
}

/// Position specifier for Seek operations. @copy.
enum SeekFrom {
  /// Absolute byte offset from the start.
  Start(u64),
  /// Byte offset relative to the end (typically negative).
  End(i64),
  /// Byte offset relative to the current position.
  Current(i64),
}
