// WASI random imports for CSPRNG.

#ifdef __wasm__

extern int __imported_wasi_snapshot_preview1_random_get(
    void *buf, unsigned int buf_len)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("random_get")));

void __asc_random_get(void *buf, unsigned int len) {
  __imported_wasi_snapshot_preview1_random_get(buf, len);
}

#else

#include <fcntl.h>
#include <unistd.h>

void __asc_random_get(void *buf, unsigned int len) {
  int fd = open("/dev/urandom", 0); // O_RDONLY
  if (fd >= 0) {
    read(fd, buf, len);
    close(fd);
  }
}

#endif
