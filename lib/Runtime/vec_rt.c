// Vec runtime functions for the asc compiler.
// Vec layout: { ptr: void*, len: unsigned long, cap: unsigned long }
// Vecs are heap-allocated, growable arrays of fixed-size elements.

#include <string.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);

typedef struct {
  char *ptr;
  unsigned long len;
  unsigned long cap;
} AscVec;

// Create a new empty vec.
AscVec *__asc_vec_new(unsigned int elem_size) {
  AscVec *v = (AscVec *)malloc(sizeof(AscVec));
  v->ptr = 0;
  v->len = 0;
  v->cap = 0;
  (void)elem_size; // Used for future type-specific initialization.
  return v;
}

// Push an element onto the vec (copies elem_size bytes from elem_ptr).
void __asc_vec_push(AscVec *v, const void *elem_ptr, unsigned int elem_size) {
  if (!v) return;
  if (v->len >= v->cap) {
    unsigned long new_cap = v->cap * 2;
    if (new_cap < 4) new_cap = 4;
    char *new_ptr = (char *)malloc(new_cap * elem_size);
    if (v->ptr && v->len > 0)
      memcpy(new_ptr, v->ptr, v->len * elem_size);
    if (v->ptr)
      free(v->ptr);
    v->ptr = new_ptr;
    v->cap = new_cap;
  }
  memcpy(v->ptr + v->len * elem_size, elem_ptr, elem_size);
  v->len++;
}

// Get element at index. Returns pointer to element or NULL if out of bounds.
const void *__asc_vec_get(AscVec *v, unsigned long index,
                          unsigned int elem_size) {
  if (!v || index >= v->len) return 0;
  return v->ptr + index * elem_size;
}

// Get vec length.
unsigned long __asc_vec_len(AscVec *v) {
  return v ? v->len : 0;
}

// Pop last element. Returns 1 if successful, 0 if empty.
// Copies the removed element into out_ptr.
int __asc_vec_pop(AscVec *v, void *out_ptr, unsigned int elem_size) {
  if (!v || v->len == 0) return 0;
  v->len--;
  if (out_ptr)
    memcpy(out_ptr, v->ptr + v->len * elem_size, elem_size);
  return 1;
}

// Free a vec and its data.
void __asc_vec_free(AscVec *v) {
  if (v) {
    if (v->ptr) free(v->ptr);
    free(v);
  }
}
