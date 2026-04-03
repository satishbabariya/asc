// WASI clock imports for the asc standard library.

#ifdef __wasm__

extern int __imported_wasi_snapshot_preview1_clock_time_get(
    int clock_id, long long precision, long long *time)
    __attribute__((import_module("wasi_snapshot_preview1"),
                   import_name("clock_time_get")));

long long __asc_monotonic_ns(void) {
  long long t;
  __imported_wasi_snapshot_preview1_clock_time_get(1, 1, &t);
  return t;
}

long long __asc_realtime_ns(void) {
  long long t;
  __imported_wasi_snapshot_preview1_clock_time_get(0, 1, &t);
  return t;
}

#else

#include <time.h>

long long __asc_monotonic_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

long long __asc_realtime_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// --- Convenience wrappers ---

void __asc_sleep_ms(unsigned long ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, 0);
}

unsigned long __asc_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long)ts.tv_sec * 1000 + (unsigned long)ts.tv_nsec / 1000000;
}

#endif
