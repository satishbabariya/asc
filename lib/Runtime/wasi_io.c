// WASI I/O support for println!/print! macros.

#ifdef __wasm__

// WASI fd_write import.
// int fd_write(int fd, const __wasi_ciovec_t *iovs, int iovs_len, int *nwritten);
typedef struct {
  const char *buf;
  unsigned int buf_len;
} __wasi_ciovec_t;

extern int __imported_wasi_snapshot_preview1_fd_write(
    int fd, const __wasi_ciovec_t *iovs, int iovs_len, unsigned int *nwritten)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("fd_write")));

void __asc_print(const char *ptr, unsigned int len) {
  __wasi_ciovec_t iov = {ptr, len};
  unsigned int nwritten;
  __imported_wasi_snapshot_preview1_fd_write(1, &iov, 1, &nwritten);
}

void __asc_eprint(const char *ptr, unsigned int len) {
  __wasi_ciovec_t iov = {ptr, len};
  unsigned int nwritten;
  __imported_wasi_snapshot_preview1_fd_write(2, &iov, 1, &nwritten);
}

#else

// Native: use write() syscall.
extern long write(int fd, const void *buf, unsigned long count);

void __asc_print(const char *ptr, unsigned int len) {
  write(1, ptr, len);
}

void __asc_eprint(const char *ptr, unsigned int len) {
  write(2, ptr, len);
}

#endif
