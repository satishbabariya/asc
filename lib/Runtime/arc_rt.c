// Arc (Atomic Reference Counted) runtime for the asc compiler.
// Layout: { ref_count: uint32_t (atomic), data[] }

#include <stdint.h>
#include <string.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);

typedef struct {
  uint32_t ref_count; // atomic
  // data follows at offset 4 (aligned to 8 via padding)
  uint32_t _pad;
  // user data starts at offset 8
} AscArc;

#define ARC_DATA_OFFSET 8

void *__asc_arc_new(const void *data, unsigned int data_size) {
  AscArc *arc = (AscArc *)malloc(ARC_DATA_OFFSET + data_size);
  __atomic_store_n(&arc->ref_count, 1, __ATOMIC_RELEASE);
  arc->_pad = 0;
  if (data && data_size > 0)
    memcpy((char *)arc + ARC_DATA_OFFSET, data, data_size);
  return arc;
}

void *__asc_arc_clone(void *arc_ptr) {
  AscArc *arc = (AscArc *)arc_ptr;
  if (arc)
    __atomic_fetch_add(&arc->ref_count, 1, __ATOMIC_RELAXED);
  return arc_ptr;
}

void __asc_arc_drop(void *arc_ptr) {
  AscArc *arc = (AscArc *)arc_ptr;
  if (!arc) return;
  if (__atomic_sub_fetch(&arc->ref_count, 1, __ATOMIC_ACQ_REL) == 0) {
    free(arc);
  }
}

const void *__asc_arc_get(void *arc_ptr) {
  if (!arc_ptr) return 0;
  return (const char *)arc_ptr + ARC_DATA_OFFSET;
}

unsigned int __asc_arc_strong_count(void *arc_ptr) {
  AscArc *arc = (AscArc *)arc_ptr;
  if (!arc) return 0;
  return __atomic_load_n(&arc->ref_count, __ATOMIC_ACQUIRE);
}
