// WASI random imports for CSPRNG.

#ifdef __wasm__

extern int __imported_wasi_snapshot_preview1_random_get(
    void *buf, unsigned int buf_len)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("random_get")));

int __asc_random_get(void *buf, unsigned int len) {
  return __imported_wasi_snapshot_preview1_random_get(buf, len);
}

#else

#include <fcntl.h>
#include <unistd.h>

int __asc_random_get(void *buf, unsigned int len) {
  int fd = open("/dev/urandom", 0); // O_RDONLY
  if (fd < 0) return -1;
  long n = read(fd, buf, len);
  close(fd);
  return (n == (long)len) ? 0 : -1;
}

#endif
