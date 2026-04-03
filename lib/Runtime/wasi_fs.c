// WASI filesystem imports for the asc standard library.

#ifdef __wasm__

// WASI filesystem types.
typedef unsigned int __wasi_fd_t;
typedef unsigned long long __wasi_filesize_t;
typedef unsigned long long __wasi_rights_t;

typedef struct {
  const char *buf;
  unsigned int buf_len;
} __wasi_ciovec_t;

typedef struct {
  char *buf;
  unsigned int buf_len;
} __wasi_iovec_t;

// WASI imports.
extern int __imported_wasi_snapshot_preview1_fd_read(
    __wasi_fd_t fd, const __wasi_iovec_t *iovs, int iovs_len,
    unsigned int *nread)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("fd_read")));

extern int __imported_wasi_snapshot_preview1_fd_close(
    __wasi_fd_t fd)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("fd_close")));

extern int __imported_wasi_snapshot_preview1_fd_seek(
    __wasi_fd_t fd, long long offset, int whence, long long *newoffset)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("fd_seek")));

extern int __imported_wasi_snapshot_preview1_path_open(
    __wasi_fd_t dirfd, int dirflags, const char *path, int path_len,
    int oflags, __wasi_rights_t rights, __wasi_rights_t rights_inheriting,
    int fdflags, __wasi_fd_t *fd)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("path_open")));

extern int __imported_wasi_snapshot_preview1_fd_filestat_get(
    __wasi_fd_t fd, void *buf)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("fd_filestat_get")));

// C wrappers for the standard library.
int __asc_fd_read(int fd, void *buf, unsigned int buf_len, unsigned int *nread) {
  __wasi_iovec_t iov = { (char *)buf, buf_len };
  return __imported_wasi_snapshot_preview1_fd_read(fd, &iov, 1, nread);
}

int __asc_fd_close(int fd) {
  return __imported_wasi_snapshot_preview1_fd_close(fd);
}

int __asc_fd_seek(int fd, long long offset, int whence, long long *newoffset) {
  return __imported_wasi_snapshot_preview1_fd_seek(fd, offset, whence, newoffset);
}

int __asc_path_open(int dirfd, const char *path, int path_len,
                    int oflags, int *result_fd) {
  __wasi_fd_t fd;
  __wasi_rights_t rights = 0xFFFFFFFFFFFFFFFFull; // all rights
  int err = __imported_wasi_snapshot_preview1_path_open(
      dirfd, 0, path, path_len, oflags, rights, rights, 0, &fd);
  *result_fd = (int)fd;
  return err;
}

#else

// Native: use POSIX syscalls.
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int __asc_fd_read(int fd, void *buf, unsigned int buf_len, unsigned int *nread) {
  long n = read(fd, buf, buf_len);
  if (n < 0) { *nread = 0; return -1; }
  *nread = (unsigned int)n;
  return 0;
}

int __asc_fd_close(int fd) {
  return close(fd);
}

int __asc_fd_seek(int fd, long long offset, int whence, long long *newoffset) {
  long long pos = lseek(fd, offset, whence);
  if (pos < 0) return -1;
  *newoffset = pos;
  return 0;
}

int __asc_path_open(int dirfd, const char *path, int path_len,
                    int oflags, int *result_fd) {
  // DECISION: Null-terminate the path (copy to stack buffer).
  char buf[4096];
  if (path_len >= 4096) return -1;
  for (int i = 0; i < path_len; i++) buf[i] = path[i];
  buf[path_len] = '\0';
  int fd = open(buf, oflags, 0666);
  if (fd < 0) return -1;
  *result_fd = fd;
  return 0;
}

#endif
