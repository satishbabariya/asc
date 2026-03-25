// std/io/error.ts — IoError and ErrorKind (RFC-0014)

/// Kinds of I/O errors, mapping to common WASI/POSIX error codes.
enum ErrorKind {
  NotFound,
  PermissionDenied,
  ConnectionRefused,
  ConnectionReset,
  ConnectionAborted,
  NotConnected,
  AddrInUse,
  AddrNotAvailable,
  BrokenPipe,
  AlreadyExists,
  WouldBlock,
  InvalidInput,
  InvalidData,
  TimedOut,
  WriteZero,
  Interrupted,
  UnexpectedEof,
  OutOfMemory,
  Unsupported,
  Other,
}

/// An I/O error containing a kind and an optional message.
struct IoError {
  kind: ErrorKind,
  message: own<String>,
}

impl IoError {
  /// Creates a new IoError with the given kind and message.
  fn new(kind: ErrorKind, msg: ref<str>): IoError {
    return IoError { kind: kind, message: String::from(msg) };
  }

  /// Returns the error kind.
  fn kind(ref<Self>): ErrorKind {
    return self.kind;
  }

  /// Creates an IoError from a WASI errno value.
  fn from_wasi_errno(errno: i32): IoError {
    const kind = match errno {
      2 => ErrorKind::NotFound,           // ENOENT
      13 => ErrorKind::PermissionDenied,  // EACCES
      17 => ErrorKind::AlreadyExists,     // EEXIST
      22 => ErrorKind::InvalidInput,      // EINVAL
      28 => ErrorKind::OutOfMemory,       // ENOSPC
      32 => ErrorKind::BrokenPipe,        // EPIPE
      35 => ErrorKind::WouldBlock,        // EAGAIN
      61 => ErrorKind::ConnectionRefused, // ECONNREFUSED
      54 => ErrorKind::ConnectionReset,   // ECONNRESET
      110 => ErrorKind::TimedOut,         // ETIMEDOUT
      _ => ErrorKind::Other,
    };
    let msg = String::from("WASI errno: ");
    // Append errno as string (simplified).
    msg.push((48 + (errno / 10) % 10) as char);
    msg.push((48 + errno % 10) as char);
    return IoError { kind: kind, message: msg };
  }
}

impl Display for IoError {
  fn fmt(ref<Self>, f: refmut<Formatter>): Result<void, FmtError> {
    const kind_str = match self.kind {
      ErrorKind::NotFound => "entity not found",
      ErrorKind::PermissionDenied => "permission denied",
      ErrorKind::ConnectionRefused => "connection refused",
      ErrorKind::ConnectionReset => "connection reset",
      ErrorKind::ConnectionAborted => "connection aborted",
      ErrorKind::NotConnected => "not connected",
      ErrorKind::AddrInUse => "address in use",
      ErrorKind::AddrNotAvailable => "address not available",
      ErrorKind::BrokenPipe => "broken pipe",
      ErrorKind::AlreadyExists => "entity already exists",
      ErrorKind::WouldBlock => "operation would block",
      ErrorKind::InvalidInput => "invalid input parameter",
      ErrorKind::InvalidData => "invalid data",
      ErrorKind::TimedOut => "timed out",
      ErrorKind::WriteZero => "write zero",
      ErrorKind::Interrupted => "operation interrupted",
      ErrorKind::UnexpectedEof => "unexpected end of file",
      ErrorKind::OutOfMemory => "out of memory",
      ErrorKind::Unsupported => "unsupported",
      ErrorKind::Other => "other error",
    };
    f.write_str(kind_str)?;
    if self.message.len() > 0 {
      f.write_str(": ")?;
      f.write_str(self.message.as_str())?;
    }
    return Result::Ok(());
  }
}

impl Error for IoError {
  fn message(ref<Self>): ref<str> {
    return self.message.as_str();
  }

  fn source(ref<Self>): Option<ref<dyn Error>> {
    return Option::None;
  }
}

impl Drop for IoError {
  fn drop(refmut<Self>): void {
    // message (owned String) is dropped automatically.
  }
}
