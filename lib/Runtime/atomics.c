// C11 atomic wrappers for the asc standard library.
// Used by std/sync/atomic.ts and Mutex/RwLock/Condvar.

#ifndef __wasm__
#include <stdatomic.h>
#endif

// Ordering values matching RFC-0014:
// 0=Relaxed, 1=Acquire, 2=Release, 3=AcqRel, 4=SeqCst

#ifdef __wasm__

// Wasm atomics: use __atomic builtins (available with -matomics).
int __asc_atomic_load_i32(volatile int *ptr, int order) {
  return __atomic_load_n(ptr, order);
}

void __asc_atomic_store_i32(volatile int *ptr, int val, int order) {
  __atomic_store_n(ptr, val, order);
}

int __asc_atomic_fetch_add_i32(volatile int *ptr, int val, int order) {
  return __atomic_fetch_add(ptr, val, order);
}

int __asc_atomic_fetch_sub_i32(volatile int *ptr, int val, int order) {
  return __atomic_fetch_sub(ptr, val, order);
}

int __asc_atomic_exchange_i32(volatile int *ptr, int val, int order) {
  return __atomic_exchange_n(ptr, val, order);
}

int __asc_atomic_compare_exchange_i32(volatile int *ptr, int *expected,
                                       int desired, int success_order,
                                       int fail_order) {
  return __atomic_compare_exchange_n(ptr, expected, desired, 0,
                                      success_order, fail_order);
}

// 64-bit variants.
long long __asc_atomic_load_i64(volatile long long *ptr, int order) {
  return __atomic_load_n(ptr, order);
}

void __asc_atomic_store_i64(volatile long long *ptr, long long val, int order) {
  __atomic_store_n(ptr, val, order);
}

long long __asc_atomic_fetch_add_i64(volatile long long *ptr, long long val,
                                      int order) {
  return __atomic_fetch_add(ptr, val, order);
}

#else

// Native: use C11 atomics.
int __asc_atomic_load_i32(volatile int *ptr, int order) {
  return atomic_load_explicit((_Atomic int *)ptr, (memory_order)order);
}

void __asc_atomic_store_i32(volatile int *ptr, int val, int order) {
  atomic_store_explicit((_Atomic int *)ptr, val, (memory_order)order);
}

int __asc_atomic_fetch_add_i32(volatile int *ptr, int val, int order) {
  return atomic_fetch_add_explicit((_Atomic int *)ptr, val,
                                    (memory_order)order);
}

int __asc_atomic_fetch_sub_i32(volatile int *ptr, int val, int order) {
  return atomic_fetch_sub_explicit((_Atomic int *)ptr, val,
                                    (memory_order)order);
}

int __asc_atomic_exchange_i32(volatile int *ptr, int val, int order) {
  return atomic_exchange_explicit((_Atomic int *)ptr, val,
                                   (memory_order)order);
}

int __asc_atomic_compare_exchange_i32(volatile int *ptr, int *expected,
                                       int desired, int success_order,
                                       int fail_order) {
  return atomic_compare_exchange_strong_explicit(
      (_Atomic int *)ptr, expected, desired,
      (memory_order)success_order, (memory_order)fail_order);
}

long long __asc_atomic_load_i64(volatile long long *ptr, int order) {
  return atomic_load_explicit((_Atomic long long *)ptr, (memory_order)order);
}

void __asc_atomic_store_i64(volatile long long *ptr, long long val, int order) {
  atomic_store_explicit((_Atomic long long *)ptr, val, (memory_order)order);
}

long long __asc_atomic_fetch_add_i64(volatile long long *ptr, long long val,
                                      int order) {
  return atomic_fetch_add_explicit((_Atomic long long *)ptr, val,
                                    (memory_order)order);
}

#endif
