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

// --- RwLock ---
// Readers-writer lock using atomic operations.
// Layout: { readers: int32_t (atomic), writer: int32_t (atomic) }

typedef struct {
  int32_t readers;  // atomic: count of active readers
  int32_t writer;   // atomic: 1 if writer holds lock, 0 otherwise
} AscRwLock;

void *__asc_rwlock_new(void) {
  AscRwLock *rw = (AscRwLock *)calloc(1, sizeof(AscRwLock));
  __atomic_store_n(&rw->readers, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&rw->writer, 0, __ATOMIC_RELEASE);
  return rw;
}

void __asc_rwlock_read_lock(void *ptr) {
  AscRwLock *rw = (AscRwLock *)ptr;
  if (!rw) return;
  while (1) {
    // Wait until no writer.
    while (__atomic_load_n(&rw->writer, __ATOMIC_ACQUIRE) != 0) {
#ifdef __x86_64__
      __builtin_ia32_pause();
#endif
      sched_yield();
    }
    __atomic_fetch_add(&rw->readers, 1, __ATOMIC_ACQUIRE);
    // Double-check no writer snuck in.
    if (__atomic_load_n(&rw->writer, __ATOMIC_ACQUIRE) == 0)
      return; // Got read lock.
    __atomic_fetch_sub(&rw->readers, 1, __ATOMIC_RELEASE);
  }
}

void __asc_rwlock_read_unlock(void *ptr) {
  AscRwLock *rw = (AscRwLock *)ptr;
  if (!rw) return;
  __atomic_fetch_sub(&rw->readers, 1, __ATOMIC_RELEASE);
}

void __asc_rwlock_write_lock(void *ptr) {
  AscRwLock *rw = (AscRwLock *)ptr;
  if (!rw) return;
  while (1) {
    int32_t expected = 0;
    if (__atomic_compare_exchange_n(&rw->writer, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      // Wait for readers to drain.
      while (__atomic_load_n(&rw->readers, __ATOMIC_ACQUIRE) != 0) {
#ifdef __x86_64__
        __builtin_ia32_pause();
#endif
        sched_yield();
      }
      return;
    }
#ifdef __x86_64__
    __builtin_ia32_pause();
#endif
    sched_yield();
  }
}

void __asc_rwlock_write_unlock(void *ptr) {
  AscRwLock *rw = (AscRwLock *)ptr;
  if (!rw) return;
  __atomic_store_n(&rw->writer, 0, __ATOMIC_RELEASE);
}

void __asc_rwlock_free(void *ptr) {
  free(ptr);
}
