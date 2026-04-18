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

  /// Returns a borrow of the internal buffer, refilling it from the underlying
  /// reader if empty. Per RFC-0014 §8: zero-copy access to buffered bytes.
  /// Call `consume(n)` after inspecting the slice to advance the cursor.
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

  /// Marks the first `amt` bytes of the buffer as consumed. Subsequent reads
  /// will return bytes beyond these. Clamped to buffer contents.
  fn consume(refmut<Self>, amt: usize): void {
    self.pos = self.pos + amt;
    if self.pos > self.filled { self.pos = self.filled; }
  }

  /// Reads bytes into `out` until the delimiter `byte` is found (inclusive)
  /// or EOF. Returns the number of bytes pushed to `out`.
  ///
  /// This loops over `fill_buf`/`consume`, handling short reads correctly.
  fn read_until(refmut<Self>, byte: u8, out: refmut<Vec<u8>>): Result<usize, IoError> {
    let total: usize = 0;
    loop {
      // Find the delimiter in the currently buffered window, or copy whole
      // window and refill.
      let done = false;
      let used: usize = 0;
      {
        const available = self.fill_buf()?;
        const alen = available.len();
        if alen == 0 {
          // EOF before delimiter.
          return Result::Ok(total);
        }
        let i: usize = 0;
        while i < alen {
          const b = *available.get(i).unwrap();
          out.push(b);
          i = i + 1;
          if b == byte {
            done = true;
            break;
          }
        }
        used = i;
      }
      self.consume(used);
      total = total + used;
      if done { return Result::Ok(total); }
    }
  }

  /// Reads bytes into `out` until a newline (`\n`) is found (inclusive) or
  /// EOF. Returns the number of bytes appended to `out`.
  ///
  /// This is UTF-8-aware in that it only splits at the ASCII `\n` byte —
  /// which is always a character boundary in UTF-8 — preserving any multi-byte
  /// sequences in the line.
  fn read_line(refmut<Self>, out: refmut<String>): Result<usize, IoError> {
    let bytes = Vec::new();
    const n = self.read_until(10 as u8, &mut bytes)?;
    // Append bytes to the String as UTF-8 (the underlying storage is bytes).
    let i: usize = 0;
    while i < bytes.len() {
      const b = *bytes.get(i).unwrap();
      out.push(b as char);
      i = i + 1;
    }
    return Result::Ok(n);
  }

  /// Returns an iterator over the lines of this reader.
  /// Each item is `Result<own<String>, IoError>`. The trailing `\n` and
  /// an optional `\r` before it are stripped from each line.
  fn lines(own<Self>): own<BufLines<R>> {
    return BufLines { reader: self };
  }
}

/// Iterator over lines of a BufReader. Produced by `BufReader::lines`.
/// Named `BufLines` to avoid clashing with `str::Lines` in `std/string.ts`.
struct BufLines<R: Read> {
  reader: own<BufReader<R>>,
}

impl<R: Read> Iterator for BufLines<R> {
  type Item = Result<own<String>, IoError>;

  fn next(refmut<Self>): Option<Self::Item> {
    let line = String::new();
    match self.reader.read_line(&mut line) {
      Result::Ok(n) => {
        if n == 0 { return Option::None; }
        // Strip trailing \n and an optional preceding \r (CRLF handling).
        if line.ends_with("\n") {
          line.truncate(line.len() - 1);
          if line.ends_with("\r") {
            line.truncate(line.len() - 1);
          }
        }
        return Option::Some(Result::Ok(line));
      },
      Result::Err(e) => { return Option::Some(Result::Err(e)); },
    }
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
    // Use write_all to tolerate short writes on the underlying sink.
    if buf.len() >= self.cap {
      self.inner.write_all(buf)?;
      return Result::Ok(buf.len());
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
    // Best-effort flush on drop. Errors are swallowed: drop cannot fail.
    if self.len > 0 {
      const data = unsafe { slice::from_raw_parts(self.buf, self.len) };
      let _ = self.inner.write_all(data);
    }
    if self.buf != null { free(self.buf); }
    // inner is dropped automatically (owned).
  }
}

// ---------- LineWriter ----------

/// A Write adapter that flushes on every newline (`\n`).
/// Useful for line-buffered output (e.g., stdout in interactive contexts)
/// where each line should be visible before the next line is written.
struct LineWriter<W: Write> {
  inner: own<BufWriter<W>>,
}

impl<W: Write> LineWriter<W> {
  /// Creates a new LineWriter with the default capacity (1024 bytes).
  fn new(inner: own<W>): own<LineWriter<W>> {
    return LineWriter::with_capacity(1024, inner);
  }

  /// Creates a new LineWriter with the given capacity.
  fn with_capacity(cap: usize, inner: own<W>): own<LineWriter<W>> {
    return LineWriter { inner: BufWriter::with_capacity(cap, inner) };
  }

  /// Consumes the LineWriter, flushing and returning the underlying writer.
  fn into_inner(own<Self>): Result<own<W>, IntoInnerError<W>> {
    return self.inner.into_inner();
  }
}

impl<W: Write> Write for LineWriter<W> {
  fn write(refmut<Self>, buf: ref<[u8]>): Result<usize, IoError> {
    // Find the last newline in `buf`. If present, write up to and including
    // it, flush, then buffer any remainder. If absent, just buffer. Searching
    // backwards coalesces batched lines into a single flush.
    const blen = buf.len();
    let newline_end: usize = 0;
    let i: usize = blen;
    while i > 0 {
      i = i - 1;
      if *buf.get(i).unwrap() == (10 as u8) {
        newline_end = i + 1;
        break;
      }
    }
    if newline_end == 0 {
      // No newline — straight into the buffer.
      return self.inner.write(buf);
    }
    // Write [0..newline_end) to the buffer, flush, then write any tail.
    const head = buf.slice(0, newline_end);
    let total = self.inner.write(head)?;
    self.inner.flush()?;
    if newline_end < blen {
      const tail = buf.slice(newline_end, blen);
      total = total + self.inner.write(tail)?;
    }
    return Result::Ok(total);
  }

  fn flush(refmut<Self>): Result<void, IoError> {
    return self.inner.flush();
  }
}

impl<W: Write> Drop for LineWriter<W> {
  fn drop(refmut<Self>): void {
    // BufWriter's Drop handles the final flush.
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
