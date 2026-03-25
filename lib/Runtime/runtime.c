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
  write(2, "panic: ", 7);
  write(2, msg, msg_len);
  write(2, "\n", 1);
  abort();
#endif
}

// _start entry point for Wasm.
extern int main(void);

#ifdef __wasm__
void _start(void) {
  int ret = main();
  (void)ret;
}
#endif
