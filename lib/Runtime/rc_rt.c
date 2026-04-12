// Rc (Reference Counted) runtime for the asc compiler.
// Non-atomic — single-threaded only. For multi-threaded, use Arc<T>.
// Layout: { strong_count: uint32_t, weak_count: uint32_t, data[] }

#include <stdint.h>
#include <string.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);

typedef struct {
  uint32_t strong_count;
  uint32_t weak_count;
  // user data starts at offset 8
} AscRc;

#define RC_DATA_OFFSET 8

void *__asc_rc_new(const void *data, unsigned int data_size) {
  AscRc *rc = (AscRc *)malloc(RC_DATA_OFFSET + data_size);
  rc->strong_count = 1;
  rc->weak_count = 0;
  if (data && data_size > 0)
    memcpy((char *)rc + RC_DATA_OFFSET, data, data_size);
  return rc;
}

void *__asc_rc_clone(void *rc_ptr) {
  AscRc *rc = (AscRc *)rc_ptr;
  if (rc) rc->strong_count++;
  return rc_ptr;
}

void __asc_rc_drop(void *rc_ptr) {
  AscRc *rc = (AscRc *)rc_ptr;
  if (!rc) return;
  rc->strong_count--;
  if (rc->strong_count == 0 && rc->weak_count == 0)
    free(rc);
}

const void *__asc_rc_get(void *rc_ptr) {
  if (!rc_ptr) return 0;
  return (const char *)rc_ptr + RC_DATA_OFFSET;
}

unsigned int __asc_rc_strong_count(void *rc_ptr) {
  AscRc *rc = (AscRc *)rc_ptr;
  return rc ? rc->strong_count : 0;
}

// Weak reference functions
void *__asc_rc_downgrade(void *rc_ptr) {
  AscRc *rc = (AscRc *)rc_ptr;
  if (rc) rc->weak_count++;
  return rc_ptr;
}

void *__asc_rc_upgrade(void *weak_ptr) {
  AscRc *rc = (AscRc *)weak_ptr;
  if (!rc || rc->strong_count == 0) return 0;
  rc->strong_count++;
  return weak_ptr;
}

void __asc_weak_drop(void *weak_ptr) {
  AscRc *rc = (AscRc *)weak_ptr;
  if (!rc) return;
  rc->weak_count--;
  if (rc->strong_count == 0 && rc->weak_count == 0)
    free(rc);
}

unsigned int __asc_rc_weak_count(void *rc_ptr) {
  AscRc *rc = (AscRc *)rc_ptr;
  return rc ? rc->weak_count : 0;
}
