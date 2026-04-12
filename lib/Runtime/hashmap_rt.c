// HashMap runtime functions for the asc compiler.
// Simple open-addressing hash map with linear probing.
// Keys and values are stored as fixed-size byte arrays.

#include <string.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);

typedef struct {
  unsigned char *buckets;  // array of (occupied:1 + key + value) entries
  unsigned long count;
  unsigned long capacity;
  unsigned int key_size;
  unsigned int val_size;
  unsigned int entry_size; // 1 + key_size + val_size
} AscHashMap;

static unsigned long hash_bytes(const void *data, unsigned int len) {
  const unsigned char *p = (const unsigned char *)data;
  unsigned long h = 14695981039346656037ULL;
  for (unsigned int i = 0; i < len; i++) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  return h;
}

AscHashMap *__asc_hashmap_new(unsigned int key_size, unsigned int val_size) {
  AscHashMap *m = (AscHashMap *)malloc(sizeof(AscHashMap));
  m->key_size = key_size;
  m->val_size = val_size;
  m->entry_size = 1 + key_size + val_size;
  m->count = 0;
  m->capacity = 16;
  m->buckets = (unsigned char *)malloc(m->capacity * m->entry_size);
  memset(m->buckets, 0, m->capacity * m->entry_size);
  return m;
}

static void hashmap_grow(AscHashMap *m) {
  unsigned long old_cap = m->capacity;
  unsigned char *old_buckets = m->buckets;
  m->capacity *= 2;
  m->buckets = (unsigned char *)malloc(m->capacity * m->entry_size);
  memset(m->buckets, 0, m->capacity * m->entry_size);
  m->count = 0;

  for (unsigned long i = 0; i < old_cap; i++) {
    unsigned char *entry = old_buckets + i * m->entry_size;
    if (entry[0]) {
      // Re-insert into new table.
      const void *key = entry + 1;
      const void *val = entry + 1 + m->key_size;
      unsigned long h = hash_bytes(key, m->key_size) % m->capacity;
      while (m->buckets[h * m->entry_size]) {
        h = (h + 1) % m->capacity;
      }
      unsigned char *new_entry = m->buckets + h * m->entry_size;
      new_entry[0] = 1;
      memcpy(new_entry + 1, key, m->key_size);
      memcpy(new_entry + 1 + m->key_size, val, m->val_size);
      m->count++;
    }
  }
  free(old_buckets);
}

void __asc_hashmap_insert(AscHashMap *m, const void *key, const void *val) {
  if (!m) return;
  if (m->count * 4 >= m->capacity * 3)
    hashmap_grow(m);

  unsigned long h = hash_bytes(key, m->key_size) % m->capacity;
  while (1) {
    unsigned char *entry = m->buckets + h * m->entry_size;
    if (!entry[0]) {
      // Empty slot — insert.
      entry[0] = 1;
      memcpy(entry + 1, key, m->key_size);
      memcpy(entry + 1 + m->key_size, val, m->val_size);
      m->count++;
      return;
    }
    if (memcmp(entry + 1, key, m->key_size) == 0) {
      // Key exists — update value.
      memcpy(entry + 1 + m->key_size, val, m->val_size);
      return;
    }
    h = (h + 1) % m->capacity;
  }
}

const void *__asc_hashmap_get(AscHashMap *m, const void *key) {
  if (!m || m->count == 0) return 0;
  unsigned long h = hash_bytes(key, m->key_size) % m->capacity;
  unsigned long start = h;
  while (1) {
    unsigned char *entry = m->buckets + h * m->entry_size;
    if (!entry[0]) return 0; // Empty slot — not found.
    if (memcmp(entry + 1, key, m->key_size) == 0) {
      return entry + 1 + m->key_size; // Return pointer to value.
    }
    h = (h + 1) % m->capacity;
    if (h == start) return 0; // Full loop — not found.
  }
}

int __asc_hashmap_contains(AscHashMap *m, const void *key) {
  return __asc_hashmap_get(m, key) != 0;
}

unsigned long __asc_hashmap_len(AscHashMap *m) {
  return m ? m->count : 0;
}

void __asc_hashmap_remove(AscHashMap *m, const void *key) {
  if (!m || m->count == 0) return;
  unsigned long h = hash_bytes(key, m->key_size) % m->capacity;
  unsigned long start = h;
  while (1) {
    unsigned char *entry = m->buckets + h * m->entry_size;
    if (!entry[0]) return; // Not found.
    if (memcmp(entry + 1, key, m->key_size) == 0) {
      entry[0] = 0; // Mark as empty (tombstone-free linear probing).
      m->count--;
      // Re-insert subsequent entries to maintain probe chains.
      unsigned long j = (h + 1) % m->capacity;
      while (m->buckets[j * m->entry_size]) {
        unsigned char *next = m->buckets + j * m->entry_size;
        // Use memmove-safe re-insertion: copy key/val pointers directly
        // from the entry without a fixed-size intermediate buffer.
        unsigned int kv_size = m->key_size + m->val_size;
        unsigned char *tmp = (unsigned char *)malloc(kv_size);
        memcpy(tmp, next + 1, kv_size);
        next[0] = 0;
        m->count--;
        __asc_hashmap_insert(m, tmp, tmp + m->key_size);
        free(tmp);
        j = (j + 1) % m->capacity;
      }
      return;
    }
    h = (h + 1) % m->capacity;
    if (h == start) return;
  }
}

// Get all keys as a Vec of key copies.
void *__asc_hashmap_keys(AscHashMap *m) {
  extern void *__asc_vec_new(unsigned int elem_size);
  extern void __asc_vec_push(void *v, const void *elem_ptr, unsigned int elem_size);
  void *result = __asc_vec_new(m ? m->key_size : 4);
  if (!m) return result;
  for (unsigned long i = 0; i < m->capacity; i++) {
    unsigned char *entry = m->buckets + i * m->entry_size;
    if (entry[0]) {
      __asc_vec_push(result, entry + 1, m->key_size);
    }
  }
  return result;
}

// Get all values as a Vec of value copies.
void *__asc_hashmap_values(AscHashMap *m) {
  extern void *__asc_vec_new(unsigned int elem_size);
  extern void __asc_vec_push(void *v, const void *elem_ptr, unsigned int elem_size);
  void *result = __asc_vec_new(m ? m->val_size : 4);
  if (!m) return result;
  for (unsigned long i = 0; i < m->capacity; i++) {
    unsigned char *entry = m->buckets + i * m->entry_size;
    if (entry[0]) {
      __asc_vec_push(result, entry + 1 + m->key_size, m->val_size);
    }
  }
  return result;
}

void __asc_hashmap_free(AscHashMap *m) {
  if (m) {
    if (m->buckets) free(m->buckets);
    free(m);
  }
}
