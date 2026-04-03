// C11 atomic wrappers for the asc standard library.
// Used by std/sync/atomic.ts and Mutex/RwLock/Condvar.

#ifndef __wasm__
#include <stdatomic.h>
#endif

// Ordering values matching RFC-0014:
// 0=Relaxed, 1=Acquire, 2=Release, 3=AcqRel, 4=SeqCst
//
// Map RFC ordering (0-4) to GCC __ATOMIC_* constants.
static int map_order(int rfc_order) {
  switch (rfc_order) {
  case 0: return __ATOMIC_RELAXED;
  case 1: return __ATOMIC_ACQUIRE;
  case 2: return __ATOMIC_RELEASE;
  case 3: return __ATOMIC_ACQ_REL;
  case 4: return __ATOMIC_SEQ_CST;
  default: return __ATOMIC_SEQ_CST;
  }
}

#ifndef __wasm__
// Map RFC ordering to C11 memory_order.
static memory_order map_order_c11(int rfc_order) {
  switch (rfc_order) {
  case 0: return memory_order_relaxed;
  case 1: return memory_order_acquire;
  case 2: return memory_order_release;
  case 3: return memory_order_acq_rel;
  case 4: return memory_order_seq_cst;
  default: return memory_order_seq_cst;
  }
}
#endif

#ifdef __wasm__

// Wasm atomics: use __atomic builtins (available with -matomics).
int __asc_atomic_load_i32(volatile int *ptr, int order) {
  return __atomic_load_n(ptr, map_order(order));
}

void __asc_atomic_store_i32(volatile int *ptr, int val, int order) {
  __atomic_store_n(ptr, val, map_order(order));
}

int __asc_atomic_fetch_add_i32(volatile int *ptr, int val, int order) {
  return __atomic_fetch_add(ptr, val, map_order(order));
}

int __asc_atomic_fetch_sub_i32(volatile int *ptr, int val, int order) {
  return __atomic_fetch_sub(ptr, val, map_order(order));
}

int __asc_atomic_exchange_i32(volatile int *ptr, int val, int order) {
  return __atomic_exchange_n(ptr, val, map_order(order));
}

int __asc_atomic_compare_exchange_i32(volatile int *ptr, int *expected,
                                       int desired, int success_order,
                                       int fail_order) {
  return __atomic_compare_exchange_n(ptr, expected, desired, 0,
                                      map_order(success_order), map_order(fail_order));
}

// 64-bit variants.
long long __asc_atomic_load_i64(volatile long long *ptr, int order) {
  return __atomic_load_n(ptr, map_order(order));
}

void __asc_atomic_store_i64(volatile long long *ptr, long long val, int order) {
  __atomic_store_n(ptr, val, map_order(order));
}

long long __asc_atomic_fetch_add_i64(volatile long long *ptr, long long val,
                                      int order) {
  return __atomic_fetch_add(ptr, val, map_order(order));
}

#else

// Native: use C11 atomics.
int __asc_atomic_load_i32(volatile int *ptr, int order) {
  return atomic_load_explicit((_Atomic int *)ptr, map_order_c11(order));
}

void __asc_atomic_store_i32(volatile int *ptr, int val, int order) {
  atomic_store_explicit((_Atomic int *)ptr, val, map_order_c11(order));
}

int __asc_atomic_fetch_add_i32(volatile int *ptr, int val, int order) {
  return atomic_fetch_add_explicit((_Atomic int *)ptr, val,
                                    map_order_c11(order));
}

int __asc_atomic_fetch_sub_i32(volatile int *ptr, int val, int order) {
  return atomic_fetch_sub_explicit((_Atomic int *)ptr, val,
                                    map_order_c11(order));
}

int __asc_atomic_exchange_i32(volatile int *ptr, int val, int order) {
  return atomic_exchange_explicit((_Atomic int *)ptr, val,
                                   map_order_c11(order));
}

int __asc_atomic_compare_exchange_i32(volatile int *ptr, int *expected,
                                       int desired, int success_order,
                                       int fail_order) {
  return atomic_compare_exchange_strong_explicit(
      (_Atomic int *)ptr, expected, desired,
      map_order_c11(success_order), map_order_c11(fail_order));
}

long long __asc_atomic_load_i64(volatile long long *ptr, int order) {
  return atomic_load_explicit((_Atomic long long *)ptr, map_order_c11(order));
}

void __asc_atomic_store_i64(volatile long long *ptr, long long val, int order) {
  atomic_store_explicit((_Atomic long long *)ptr, val, map_order_c11(order));
}

long long __asc_atomic_fetch_add_i64(volatile long long *ptr, long long val,
                                      int order) {
  return atomic_fetch_add_explicit((_Atomic long long *)ptr, val,
                                    map_order_c11(order));
}

#endif
