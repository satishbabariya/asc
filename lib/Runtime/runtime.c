// asc runtime — minimal runtime support for WebAssembly and native targets.
// Provides: allocator, panic handler, _start entry, WASI I/O wrappers.

// DECISION: Use a simple bump allocator for Wasm (no free).
// Native targets delegate to libc malloc/free.

#ifdef __wasm__

// Wasm linear memory bump allocator.
extern unsigned char __heap_base;
static unsigned char *__asc_heap_ptr = &__heap_base;

void *malloc(unsigned long size) {
  // Align to 8 bytes.
  size = (size + 7u) & ~7u;
  void *ptr = __asc_heap_ptr;
  __asc_heap_ptr += size;
  return ptr;
}

void free(void *ptr) {
  // No-op for bump allocator.
  // DECISION: A real allocator (dlmalloc/wee_alloc) would be integrated
  // in a future phase for production use.
  (void)ptr;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (unsigned long i = 0; i < n; i++)
    d[i] = s[i];
  return dst;
}

void *memset(void *dst, int c, unsigned long n) {
  unsigned char *d = (unsigned char *)dst;
  for (unsigned long i = 0; i < n; i++)
    d[i] = (unsigned char)c;
  return dst;
}

#endif // __wasm__

/* ── Thread-Local Arena Allocator ────────────────────────────── */

#ifndef __wasm__
extern void *malloc(unsigned long size);
extern void free(void *ptr);
#endif

#define ASC_DEFAULT_ARENA_SIZE (1024 * 1024) /* 1 MB */

#ifdef __wasm__
static unsigned char __asc_arena_buf[ASC_DEFAULT_ARENA_SIZE];
static unsigned char *__asc_arena_ptr = __asc_arena_buf;
static unsigned char *__asc_arena_end = __asc_arena_buf + ASC_DEFAULT_ARENA_SIZE;
#else
_Thread_local static unsigned char *__asc_arena_buf = 0;
_Thread_local static unsigned char *__asc_arena_ptr = 0;
_Thread_local static unsigned char *__asc_arena_end = 0;
#endif

void __asc_arena_init(unsigned long size) {
#ifndef __wasm__
  if (__asc_arena_buf) free(__asc_arena_buf);
  __asc_arena_buf = (unsigned char *)malloc(size);
  __asc_arena_ptr = __asc_arena_buf;
  __asc_arena_end = __asc_arena_buf + size;
#endif
}

void *__asc_arena_alloc(unsigned long size, unsigned long align) {
  unsigned long addr = (unsigned long)__asc_arena_ptr;
  unsigned long aligned = (addr + align - 1) & ~(align - 1);
  unsigned char *result = (unsigned char *)aligned;
  if (result + size > __asc_arena_end) return 0;
  __asc_arena_ptr = result + size;
  return result;
}

void __asc_arena_reset(void) {
  __asc_arena_ptr = __asc_arena_buf;
}

void __asc_arena_destroy(void) {
#ifndef __wasm__
  if (__asc_arena_buf) {
    free(__asc_arena_buf);
    __asc_arena_buf = 0;
    __asc_arena_ptr = 0;
    __asc_arena_end = 0;
  }
#endif
}

// Thread-local unwind flag and panic handler for drop-on-panic.
#ifdef __wasm__
static int __asc_in_unwind = 0;
#else
#include <setjmp.h>
_Thread_local static int __asc_in_unwind = 0;
_Thread_local static jmp_buf *__asc_panic_jmpbuf = 0;
#endif

// PanicInfo — captures metadata about the most recent panic for inspection.
typedef struct {
    const char *msg;
    unsigned int msg_len;
    const char *file;
    unsigned int file_len;
    unsigned int line;
    unsigned int col;
} PanicInfo;

#ifdef __wasm__
static PanicInfo __asc_panic_info = {0, 0, 0, 0, 0, 0};
#else
_Thread_local static PanicInfo __asc_panic_info = {0, 0, 0, 0, 0, 0};
#endif

PanicInfo *__asc_get_panic_info(void) {
    return &__asc_panic_info;
}

void __asc_top_level_panic_handler(void) {
  PanicInfo *info = __asc_get_panic_info();
#ifndef __wasm__
  extern long write(int fd, const void *buf, unsigned long count);
  extern void _exit(int);
  const char *prefix = "thread 'main' panicked at '";
  write(2, prefix, 27);
  if (info->msg && info->msg_len > 0)
    write(2, info->msg, info->msg_len);
  const char *mid = "', ";
  write(2, mid, 3);
  if (info->file && info->file_len > 0)
    write(2, info->file, info->file_len);
  const char *colon = ":";
  write(2, colon, 1);
  // Write line number as decimal.
  char linebuf[16];
  int len = 0;
  unsigned int line = info->line;
  if (line == 0) { linebuf[len++] = '0'; }
  else {
    char tmp[16]; int tlen = 0;
    while (line > 0) { tmp[tlen++] = '0' + (line % 10); line /= 10; }
    for (int i = tlen - 1; i >= 0; i--) linebuf[len++] = tmp[i];
  }
  write(2, linebuf, len);
  const char *nl = "\n";
  write(2, nl, 1);
  _exit(101);
#else
  __builtin_trap();
#endif
}

// Register/clear a setjmp-based panic handler for drop-on-panic.
void __asc_set_panic_handler(void *buf) {
#ifndef __wasm__
  __asc_panic_jmpbuf = (jmp_buf *)buf;
#endif
}

void __asc_clear_panic_handler(void) {
#ifndef __wasm__
  __asc_panic_jmpbuf = 0;
#endif
}

// Panic handler: traps on Wasm, aborts on native.
// If a setjmp handler is registered, longjmp to it for cleanup first.
void __asc_panic(const char *msg, unsigned int msg_len,
                 const char *file, unsigned int file_len,
                 unsigned int line, unsigned int col) {
  if (__asc_in_unwind) {
    // Double panic — print diagnostic and abort immediately.
#ifndef __wasm__
    extern long write(int fd, const void *buf, unsigned long count);
    write(2, "thread panicked while panicking: '", 34);
    if (msg && msg_len > 0)
      write(2, msg, msg_len);
    write(2, "'\noriginal panic: '", 19);
    if (__asc_panic_info.msg && __asc_panic_info.msg_len > 0)
      write(2, __asc_panic_info.msg, __asc_panic_info.msg_len);
    write(2, "'\n", 2);
#endif
    __builtin_trap();
  }
  __asc_in_unwind = 1;

  // Store panic metadata for inspection (e.g., catch blocks, double-panic).
  __asc_panic_info.msg = msg;
  __asc_panic_info.msg_len = msg_len;
  __asc_panic_info.file = file;
  __asc_panic_info.file_len = file_len;
  __asc_panic_info.line = line;
  __asc_panic_info.col = col;

#ifdef __wasm__
  // On Wasm, trap directly. Wasm EH would use throw $panic_tag.
  __builtin_trap();
#else
  // On native, write to stderr and abort.
  // DECISION: Use write() directly to avoid stdio dependency.
  extern long write(int fd, const void *buf, unsigned long count);
  extern void abort(void);

  // Helper: write an unsigned int as decimal.
  char numbuf[12];
  int numlen;

  write(2, "panic at '", 10);
  if (msg && msg_len > 0)
    write(2, msg, msg_len);
  write(2, "'", 1);

  if (line > 0) {
    write(2, " (line ", 7);
    // Convert line number to string.
    numlen = 0;
    unsigned int n = line;
    do { numbuf[11 - numlen++] = '0' + (n % 10); n /= 10; } while (n > 0);
    write(2, numbuf + 12 - numlen, numlen);
    if (col > 0) {
      write(2, ":", 1);
      numlen = 0;
      n = col;
      do { numbuf[11 - numlen++] = '0' + (n % 10); n /= 10; } while (n > 0);
      write(2, numbuf + 12 - numlen, numlen);
    }
    write(2, ")", 1);
  }
  write(2, "\n", 1);

  // If a panic handler is registered, longjmp for cleanup.
  if (__asc_panic_jmpbuf) {
    longjmp(*__asc_panic_jmpbuf, 1);
  }

  abort();
#endif
}

// Print functions — defined in wasi_io.c for wasm, here for native only.
// Note: __asc_print and __asc_eprint are in wasi_io.c to avoid duplicates.

#ifndef __wasm__
extern long write(int fd, const void *buf, unsigned long count);

void __asc_println(const char *ptr, unsigned int len) {
  if (ptr && len > 0)
    write(1, ptr, len);
  write(1, "\n", 1);
}

void __asc_print_i32(int value) {
  char buf[12];
  int neg = 0;
  unsigned int v;
  if (value < 0) {
    neg = 1;
    v = (unsigned int)(-(value + 1)) + 1;
  } else {
    v = (unsigned int)value;
  }
  int i = 11;
  buf[i] = '\0';
  do {
    buf[--i] = '0' + (v % 10);
    v /= 10;
  } while (v > 0);
  if (neg)
    buf[--i] = '-';
  write(1, buf + i, 11 - i);
}

void __asc_print_i32_ln(int value) {
  __asc_print_i32(value);
  write(1, "\n", 1);
}
#endif

// _start entry point for Wasm.
extern int main(void);

#ifdef __wasm__
// WASI proc_exit sets the process exit code.
void __wasi_proc_exit(int code) __attribute__((__import_module__("wasi_snapshot_preview1"),
                                                __import_name__("proc_exit")));

void _start(void) {
  int ret = main();
  if (ret != 0)
    __wasi_proc_exit(ret);
}
#endif
