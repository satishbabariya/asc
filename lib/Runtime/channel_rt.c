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

void __asc_chan_clone(void *channel) {
  AscChannel *ch = (AscChannel *)channel;
  __atomic_fetch_add(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
}

void __asc_chan_drop(void *channel) {
  AscChannel *ch = (AscChannel *)channel;
  uint32_t prev = __atomic_fetch_sub(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
  if (prev == 1) {
    // Last reference — free the channel and its slot buffer.
    free(ch);
  }
}

/* ── MPMC Channel (mutex-guarded) ────────────────────────────── */

#ifndef __wasm__
#include <pthread.h>

typedef struct {
  unsigned char *buffer;
  uint32_t head;
  uint32_t tail;
  uint32_t capacity;
  uint32_t elem_size;
  uint32_t ref_count;
  pthread_mutex_t mutex;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
} AscMPMCChannel;

void *__asc_mpmc_chan_create(uint32_t capacity, uint32_t elem_size) {
  AscMPMCChannel *ch = (AscMPMCChannel *)calloc(1, sizeof(AscMPMCChannel));
  ch->buffer = (unsigned char *)calloc(capacity, elem_size);
  ch->capacity = capacity;
  ch->elem_size = elem_size;
  ch->ref_count = 2;
  pthread_mutex_init(&ch->mutex, 0);
  pthread_cond_init(&ch->not_empty, 0);
  pthread_cond_init(&ch->not_full, 0);
  return ch;
}

void __asc_mpmc_chan_send(void *channel, const void *data) {
  AscMPMCChannel *ch = (AscMPMCChannel *)channel;
  pthread_mutex_lock(&ch->mutex);
  while ((ch->tail - ch->head) >= ch->capacity)
    pthread_cond_wait(&ch->not_full, &ch->mutex);
  uint32_t slot = ch->tail % ch->capacity;
  __builtin_memcpy(ch->buffer + slot * ch->elem_size, data, ch->elem_size);
  ch->tail++;
  pthread_cond_signal(&ch->not_empty);
  pthread_mutex_unlock(&ch->mutex);
}

int __asc_mpmc_chan_recv(void *channel, void *out) {
  AscMPMCChannel *ch = (AscMPMCChannel *)channel;
  pthread_mutex_lock(&ch->mutex);
  while (ch->head >= ch->tail)
    pthread_cond_wait(&ch->not_empty, &ch->mutex);
  uint32_t slot = ch->head % ch->capacity;
  __builtin_memcpy(out, ch->buffer + slot * ch->elem_size, ch->elem_size);
  ch->head++;
  pthread_cond_signal(&ch->not_full);
  pthread_mutex_unlock(&ch->mutex);
  return 1;
}

void __asc_mpmc_chan_drop(void *channel) {
  AscMPMCChannel *ch = (AscMPMCChannel *)channel;
  uint32_t prev = __atomic_fetch_sub(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
  if (prev == 1) {
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch->buffer);
    free(ch);
  }
}

void __asc_mpmc_chan_clone(void *channel) {
  AscMPMCChannel *ch = (AscMPMCChannel *)channel;
  __atomic_fetch_add(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
}

#endif /* !__wasm__ */
