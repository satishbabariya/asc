// std/io/buf.ts — BufReader<R> and BufWriter<W> (RFC-0014)

const DEFAULT_BUF_SIZE: usize = 8192;

/// Buffered wrapper around a Read implementor.
/// Reduces the number of underlying read calls by reading in bulk.
struct BufReader<R: Read> {
  inner: own<R>,
  buf: *mut u8,
  cap: usize,
  pos: usize,   // current read position in buffer
  filled: usize, // number of valid bytes in buffer
}

impl<R: Read> BufReader<R> {
  /// Creates a new BufReader with the default buffer size (8192 bytes).
  fn new(inner: own<R>): own<BufReader<R>> {
    return BufReader::with_capacity(DEFAULT_BUF_SIZE, inner);
  }

  /// Creates a new BufReader with the specified buffer capacity.
  fn with_capacity(cap: usize, inner: own<R>): own<BufReader<R>> {
    const buf = malloc(cap) as *mut u8;
    return BufReader {
      inner: inner,
      buf: buf,
      cap: cap,
      pos: 0,
      filled: 0,
    };
  }

  /// Returns the buffered data that has been read but not yet consumed.
  fn buffer(ref<Self>): ref<[u8]> {
    return unsafe { slice::from_raw_parts(self.buf + self.pos, self.filled - self.pos) };
  }

  /// Consumes the BufReader, returning the underlying reader.
  fn into_inner(own<Self>): own<R> {
    free(self.buf);
    return self.inner;
  }

  // Internal: fill the buffer from the underlying reader.
  fn fill_buf(refmut<Self>): Result<ref<[u8]>, IoError> {
    if self.pos >= self.filled {
      // Buffer exhausted — refill.
      self.pos = 0;
      const read_buf = unsafe { slice::from_raw_parts_mut(self.buf, self.cap) };
      const n = self.inner.read(read_buf)?;
      self.filled = n;
    }
    return Result::Ok(self.buffer());
  }

  // Internal: consume `amt` bytes from the buffer.
  fn consume(refmut<Self>, amt: usize): void {
    self.pos = self.pos + amt;
    if self.pos > self.filled { self.pos = self.filled; }
  }
}

impl<R: Read> Read for BufReader<R> {
  fn read(refmut<Self>, buf: refmut<[u8]>): Result<usize, IoError> {
    const avail = self.filled - self.pos;
    if avail > 0 {
      // Serve from buffer.
      let to_copy = buf.len();
      if to_copy > avail { to_copy = avail; }
      memcpy(buf.as_mut_ptr(), self.buf + self.pos, to_copy);
      self.pos = self.pos + to_copy;
      return Result::Ok(to_copy);
    }
    // Buffer empty — if request is larger than buffer, read directly.
    if buf.len() >= self.cap {
      return self.inner.read(buf);
    }
    // Refill buffer, then copy.
    self.fill_buf()?;
    const new_avail = self.filled - self.pos;
    let to_copy = buf.len();
    if to_copy > new_avail { to_copy = new_avail; }
    memcpy(buf.as_mut_ptr(), self.buf + self.pos, to_copy);
    self.pos = self.pos + to_copy;
    return Result::Ok(to_copy);
  }
}

impl<R: Read> Drop for BufReader<R> {
  fn drop(refmut<Self>): void {
    if self.buf != null { free(self.buf); }
    // inner is dropped automatically (owned).
  }
}

// ---------- BufWriter ----------

/// Buffered wrapper around a Write implementor.
/// Flushes automatically when the buffer is full or on drop.
struct BufWriter<W: Write> {
  inner: own<W>,
  buf: *mut u8,
  cap: usize,
  len: usize,   // number of bytes in buffer
}

impl<W: Write> BufWriter<W> {
  /// Creates a new BufWriter with the default buffer size (8192 bytes).
  fn new(inner: own<W>): own<BufWriter<W>> {
    return BufWriter::with_capacity(DEFAULT_BUF_SIZE, inner);
  }

  /// Creates a new BufWriter with the specified buffer capacity.
  fn with_capacity(cap: usize, inner: own<W>): own<BufWriter<W>> {
    const buf = malloc(cap) as *mut u8;
    return BufWriter {
      inner: inner,
      buf: buf,
      cap: cap,
      len: 0,
    };
  }

  /// Returns the buffered data not yet flushed.
  fn buffer(ref<Self>): ref<[u8]> {
    return unsafe { slice::from_raw_parts(self.buf, self.len) };
  }

  /// Consumes the BufWriter, flushing and returning the underlying writer.
  fn into_inner(own<Self>): Result<own<W>, IntoInnerError<W>> {
    // Flush remaining data.
    if self.len > 0 {
      const flush_buf = unsafe { slice::from_raw_parts(self.buf, self.len) };
      match self.inner.write_all(flush_buf) {
        Result::Ok(_) => {},
        Result::Err(e) => {
          free(self.buf);
          return Result::Err(IntoInnerError { writer: self.inner, error: e });
        },
      }
    }
    free(self.buf);
    return Result::Ok(self.inner);
  }

  // Internal: flush the buffer to the underlying writer.
  fn flush_buf(refmut<Self>): Result<void, IoError> {
    if self.len == 0 { return Result::Ok(()); }
    const data = unsafe { slice::from_raw_parts(self.buf, self.len) };
    self.inner.write_all(data)?;
    self.len = 0;
    return Result::Ok(());
  }
}

impl<W: Write> Write for BufWriter<W> {
  fn write(refmut<Self>, buf: ref<[u8]>): Result<usize, IoError> {
    // If data won't fit in buffer, flush first.
    if self.len + buf.len() > self.cap {
      self.flush_buf()?;
    }
    // If data is larger than the buffer, write directly.
    if buf.len() >= self.cap {
      return self.inner.write(buf);
    }
    // Buffer the data.
    memcpy(self.buf + self.len, buf.as_ptr(), buf.len());
    self.len = self.len + buf.len();
    return Result::Ok(buf.len());
  }

  fn flush(refmut<Self>): Result<void, IoError> {
    self.flush_buf()?;
    return self.inner.flush();
  }
}

impl<W: Write> Drop for BufWriter<W> {
  fn drop(refmut<Self>): void {
    // Best-effort flush on drop.
    if self.len > 0 {
      const data = unsafe { slice::from_raw_parts(self.buf, self.len) };
      let _ = self.inner.write_all(data);
    }
    if self.buf != null { free(self.buf); }
    // inner is dropped automatically (owned).
  }
}

/// Error returned when BufWriter::into_inner fails to flush.
struct IntoInnerError<W> {
  writer: own<W>,
  error: IoError,
}

impl<W> IntoInnerError<W> {
  fn error(ref<Self>): ref<IoError> { return &self.error; }
  fn into_inner(own<Self>): own<W> { return self.writer; }
}
