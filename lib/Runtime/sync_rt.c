// Mutex and Semaphore runtime for the asc compiler.
// Mutex: spin-lock with atomic compare-exchange.
// Semaphore: counting semaphore with atomic permits.

#include <stdint.h>
#include <sched.h>

extern void *calloc(unsigned long count, unsigned long size);
extern void free(void *ptr);

// --- Mutex ---
// Layout: { state: u32 } where 0=unlocked, 1=locked

typedef struct {
  uint32_t state;
} AscMutex;

void *__asc_mutex_new(void) {
  AscMutex *m = (AscMutex *)calloc(1, sizeof(AscMutex));
  return m;
}

void __asc_mutex_lock(void *mutex) {
  AscMutex *m = (AscMutex *)mutex;
  if (!m) return;
  while (1) {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&m->state, &expected, 1,
            0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      return; // locked
#ifdef __x86_64__
    __builtin_ia32_pause();
#endif
    sched_yield();
  }
}

void __asc_mutex_unlock(void *mutex) {
  AscMutex *m = (AscMutex *)mutex;
  if (!m) return;
  __atomic_store_n(&m->state, 0, __ATOMIC_RELEASE);
}

int __asc_mutex_try_lock(void *mutex) {
  AscMutex *m = (AscMutex *)mutex;
  if (!m) return 0;
  uint32_t expected = 0;
  return __atomic_compare_exchange_n(&m->state, &expected, 1,
      0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 1 : 0;
}

void __asc_mutex_free(void *mutex) {
  free(mutex);
}

// --- Semaphore ---
// Layout: { permits: u32, max_permits: u32 }

typedef struct {
  uint32_t permits;
  uint32_t max_permits;
} AscSemaphore;

void *__asc_semaphore_new(uint32_t permits) {
  AscSemaphore *s = (AscSemaphore *)calloc(1, sizeof(AscSemaphore));
  __atomic_store_n(&s->permits, permits, __ATOMIC_RELEASE);
  s->max_permits = permits;
  return s;
}

void __asc_semaphore_acquire(void *sem) {
  AscSemaphore *s = (AscSemaphore *)sem;
  if (!s) return;
  while (1) {
    uint32_t current = __atomic_load_n(&s->permits, __ATOMIC_ACQUIRE);
    if (current > 0) {
      if (__atomic_compare_exchange_n(&s->permits, &current, current - 1,
              0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        return; // acquired
    }
#ifdef __x86_64__
    __builtin_ia32_pause();
#endif
    sched_yield();
  }
}

int __asc_semaphore_try_acquire(void *sem) {
  AscSemaphore *s = (AscSemaphore *)sem;
  if (!s) return 0;
  uint32_t current = __atomic_load_n(&s->permits, __ATOMIC_ACQUIRE);
  if (current > 0) {
    if (__atomic_compare_exchange_n(&s->permits, &current, current - 1,
            0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
      return 1;
  }
  return 0;
}

void __asc_semaphore_release(void *sem) {
  AscSemaphore *s = (AscSemaphore *)sem;
  if (!s) return;
  __atomic_fetch_add(&s->permits, 1, __ATOMIC_RELEASE);
}

uint32_t __asc_semaphore_available(void *sem) {
  AscSemaphore *s = (AscSemaphore *)sem;
  return s ? __atomic_load_n(&s->permits, __ATOMIC_ACQUIRE) : 0;
}

void __asc_semaphore_free(void *sem) {
  free(sem);
}
