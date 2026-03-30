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

// Thread-local unwind flag for double-panic detection.
static int __asc_in_unwind = 0;

// Panic handler: traps on Wasm, aborts on native.
void __asc_panic(const char *msg, unsigned int msg_len,
                 const char *file, unsigned int file_len,
                 unsigned int line, unsigned int col) {
  if (__asc_in_unwind) {
    // Double panic — abort immediately.
    __builtin_trap();
  }
  __asc_in_unwind = 1;

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
  abort();
#endif
}

// Print a string (ptr + len) to stdout.
extern long write(int fd, const void *buf, unsigned long count);

void __asc_print(const char *ptr, unsigned int len) {
  if (ptr && len > 0)
    write(1, ptr, len);
}

void __asc_println(const char *ptr, unsigned int len) {
  if (ptr && len > 0)
    write(1, ptr, len);
  write(1, "\n", 1);
}

// Print an integer to stdout.
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

// _start entry point for Wasm.
extern int main(void);

#ifdef __wasm__
void _start(void) {
  int ret = main();
  (void)ret;
}
#endif
