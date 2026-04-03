// Channel ring buffer runtime for the asc compiler.
// Lock-free SPSC (single-producer, single-consumer) channel.
// Layout: { head: u32, tail: u32, capacity: u32, ref_count: u32, slots[N] }

#include <string.h>
#include <stdint.h>

extern void *malloc(unsigned long size);
extern void *calloc(unsigned long count, unsigned long size);
extern void free(void *ptr);

typedef struct {
  uint32_t head;       // recv index (atomic)
  uint32_t tail;       // send index (atomic)
  uint32_t capacity;
  uint32_t ref_count;  // 2 = both tx+rx alive
  // slots follow in memory at offset 16
} AscChannel;

void *__asc_chan_make(uint32_t capacity, uint32_t elem_size) {
  if (capacity == 0) capacity = 4;
  uint32_t total = sizeof(AscChannel) + capacity * elem_size;
  AscChannel *ch = (AscChannel *)calloc(1, total);
  ch->head = 0;
  ch->tail = 0;
  ch->capacity = capacity;
  ch->ref_count = 2;
  return ch;
}

void __asc_chan_send(void *chan, const void *val, uint32_t elem_size) {
  AscChannel *ch = (AscChannel *)chan;
  if (!ch) return;

  // Spin-wait until not full.
  uint32_t cap = ch->capacity;
  while (__atomic_load_n(&ch->tail, __ATOMIC_ACQUIRE) -
         __atomic_load_n(&ch->head, __ATOMIC_ACQUIRE) >= cap) {
    // Yield.
#ifdef __x86_64__
    __builtin_ia32_pause();
#endif
  }

  uint32_t tail = __atomic_load_n(&ch->tail, __ATOMIC_RELAXED);
  uint32_t slot_offset = sizeof(AscChannel) + (tail % cap) * elem_size;
  memcpy((char *)chan + slot_offset, val, elem_size);
  __atomic_fetch_add(&ch->tail, 1, __ATOMIC_RELEASE);
}

void __asc_chan_recv(void *chan, void *out, uint32_t elem_size) {
  AscChannel *ch = (AscChannel *)chan;
  if (!ch) return;

  // Spin-wait until not empty.
  while (__atomic_load_n(&ch->tail, __ATOMIC_ACQUIRE) ==
         __atomic_load_n(&ch->head, __ATOMIC_ACQUIRE)) {
#ifdef __x86_64__
    __builtin_ia32_pause();
#endif
  }

  uint32_t head = __atomic_load_n(&ch->head, __ATOMIC_RELAXED);
  uint32_t cap = ch->capacity;
  uint32_t slot_offset = sizeof(AscChannel) + (head % cap) * elem_size;
  memcpy(out, (char *)chan + slot_offset, elem_size);
  __atomic_fetch_add(&ch->head, 1, __ATOMIC_RELEASE);
}

void __asc_chan_free(void *chan) {
  free(chan);
}
