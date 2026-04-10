// RUN: %asc check %s 2>&1 || true
// Test: struct with custom Drop implementation.

struct FileHandle {
  fd: i32,
}

impl Drop for FileHandle {
  fn drop(refmut<Self>): void {
    // Close the file descriptor.
  }
}

function open_file(path: i32): own<FileHandle> {
  return FileHandle { fd: path };
}

function main(): i32 {
  const f = open_file(3);
  return 0;
}
