// String runtime functions for the asc compiler.
// String layout: { ptr: char*, len: unsigned long, cap: unsigned long }
// Strings are heap-allocated, growable UTF-8 byte buffers.

#include <string.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);

// String struct layout matches the compiler's LLVM type:
// { ptr: !llvm.ptr, len: i64, cap: i64 }
typedef struct {
  char *ptr;
  unsigned long len;
  unsigned long cap;
} AscString;

// Create a new empty string on the heap.
AscString *__asc_string_new(void) {
  AscString *s = (AscString *)malloc(sizeof(AscString));
  s->ptr = 0;
  s->len = 0;
  s->cap = 0;
  return s;
}

// Create a string from static data (copies bytes to heap).
AscString *__asc_string_from(const char *data, unsigned long len) {
  AscString *s = (AscString *)malloc(sizeof(AscString));
  unsigned long cap = len < 8 ? 8 : len;
  s->ptr = (char *)malloc(cap);
  s->len = len;
  s->cap = cap;
  if (len > 0 && data)
    memcpy(s->ptr, data, len);
  return s;
}

// Append bytes to a string (realloc if needed).
void __asc_string_push_str(AscString *s, const char *data, unsigned long len) {
  if (!s) return;
  unsigned long new_len = s->len + len;
  if (new_len > s->cap) {
    unsigned long new_cap = s->cap * 2;
    if (new_cap < new_len) new_cap = new_len;
    if (new_cap < 8) new_cap = 8;
    char *new_ptr = (char *)malloc(new_cap);
    if (s->ptr && s->len > 0)
      memcpy(new_ptr, s->ptr, s->len);
    if (s->ptr)
      free(s->ptr);
    s->ptr = new_ptr;
    s->cap = new_cap;
  }
  if (data && len > 0)
    memcpy(s->ptr + s->len, data, len);
  s->len = new_len;
}

// Get string length.
unsigned long __asc_string_len(AscString *s) {
  return s ? s->len : 0;
}

// Get pointer to string data.
const char *__asc_string_as_ptr(AscString *s) {
  return s ? s->ptr : 0;
}

// Compare two strings for equality.
int __asc_string_eq(AscString *a, AscString *b) {
  if (!a || !b) return a == b;
  if (a->len != b->len) return 0;
  if (a->len == 0) return 1;
  return memcmp(a->ptr, b->ptr, a->len) == 0;
}

// Compare string to static str (ptr + len).
int __asc_string_eq_str(AscString *s, const char *data, unsigned long len) {
  if (!s) return len == 0;
  if (s->len != len) return 0;
  if (len == 0) return 1;
  return memcmp(s->ptr, data, len) == 0;
}

// Concatenate two strings into a new string.
AscString *__asc_string_concat(AscString *a, AscString *b) {
  unsigned long total = (a ? a->len : 0) + (b ? b->len : 0);
  AscString *s = (AscString *)malloc(sizeof(AscString));
  unsigned long cap = total < 8 ? 8 : total;
  s->ptr = (char *)malloc(cap);
  s->len = total;
  s->cap = cap;
  unsigned long offset = 0;
  if (a && a->len > 0) {
    memcpy(s->ptr, a->ptr, a->len);
    offset = a->len;
  }
  if (b && b->len > 0) {
    memcpy(s->ptr + offset, b->ptr, b->len);
  }
  return s;
}

// Clear a string (set len to 0, keep capacity).
void __asc_string_clear(AscString *s) {
  if (s) s->len = 0;
}

// Split string by delimiter, returns a Vec of Strings.
// For simplicity, returns a Vec<ptr> where each ptr is an AscString*.
void *__asc_string_split(AscString *s, const char *delim, unsigned long delim_len) {
  extern void *__asc_vec_new(unsigned int elem_size);
  extern void __asc_vec_push(void *v, const void *elem_ptr, unsigned int elem_size);

  void *result = __asc_vec_new(sizeof(void *));
  if (!s || !s->ptr || s->len == 0) return result;

  unsigned long start = 0;
  for (unsigned long i = 0; i <= s->len - delim_len; i++) {
    int match = 1;
    for (unsigned long j = 0; j < delim_len; j++) {
      if (s->ptr[i + j] != delim[j]) { match = 0; break; }
    }
    if (match) {
      void *part = __asc_string_from(s->ptr + start, i - start);
      __asc_vec_push(result, &part, sizeof(void *));
      start = i + delim_len;
      i += delim_len - 1;
    }
  }
  // Push remaining.
  void *last = __asc_string_from(s->ptr + start, s->len - start);
  __asc_vec_push(result, &last, sizeof(void *));
  return result;
}

// Trim whitespace from both ends.
AscString *__asc_string_trim(AscString *s) {
  if (!s || !s->ptr || s->len == 0)
    return __asc_string_from("", 0);
  unsigned long start = 0, end = s->len;
  while (start < end && (s->ptr[start] == ' ' || s->ptr[start] == '\t' ||
         s->ptr[start] == '\n' || s->ptr[start] == '\r')) start++;
  while (end > start && (s->ptr[end-1] == ' ' || s->ptr[end-1] == '\t' ||
         s->ptr[end-1] == '\n' || s->ptr[end-1] == '\r')) end--;
  return __asc_string_from(s->ptr + start, end - start);
}

// Get string length in chars (bytes for ASCII).
unsigned long __asc_string_chars_len(AscString *s) {
  return s ? s->len : 0;
}

// Get char at index (byte value).
unsigned int __asc_string_char_at(AscString *s, unsigned long index) {
  if (!s || index >= s->len) return 0;
  return (unsigned char)s->ptr[index];
}

// Free a string.
void __asc_string_free(AscString *s) {
  if (s) {
    if (s->ptr) free(s->ptr);
    free(s);
  }
}
