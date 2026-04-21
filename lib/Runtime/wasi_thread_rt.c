// wasi-threads trampoline: connects asc's task.spawn / task.join to either
// the wasi:thread-spawn import (wasm32) or pthread (host builds).
//
// Note: the wasm branch intentionally avoids system headers (same pattern as
// runtime.c), because the driver compiles for wasm32-wasi-threads without a
// sysroot. The host branch pulls in pthread.h for the unit-test build.

#if defined(__wasm__)

typedef __INT32_TYPE__  int32_t;
typedef __UINT64_TYPE__ size_t_;  // avoid <stddef.h>

extern void *malloc(unsigned long size);
extern void free(void *ptr);

struct asc_wasi_task {
    // Explicit 8-byte alignment ensures done_flag (offset 4) is 4-byte
    // aligned — required by memory.atomic.wait32 on wasm32-threads.
    int32_t tid __attribute__((aligned(8)));
    int32_t done_flag;
    // Matches the HIRBuilder-synthesized wrapper signature `ptr(ptr)` so
    // call_indirect in wasi_thread_start type-checks on wasm32. The return
    // value is ignored (see wasi_thread_start below).
    void *(*entry)(void *);
    void *arg;
};

extern int32_t __imported_wasi_thread_spawn(void *start_arg)
    __attribute__((import_module("wasi"), import_name("thread-spawn")));

__attribute__((export_name("wasi_thread_start")))
void wasi_thread_start(int32_t tid, void *start_arg) {
    struct asc_wasi_task *h = (struct asc_wasi_task *)start_arg;
    h->tid = tid;
    (void)h->entry(h->arg);
    __atomic_store_n(&h->done_flag, 1, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(&h->done_flag, 1);
}

struct asc_wasi_task *__asc_wasi_thread_spawn(void *(*entry)(void *), void *arg) {
    struct asc_wasi_task *h =
        (struct asc_wasi_task *)malloc(sizeof(struct asc_wasi_task));
    if (!h) __builtin_trap();
    h->tid = 0;
    h->done_flag = 0;
    h->entry = entry;
    h->arg = arg;
    int32_t rc = __imported_wasi_thread_spawn(h);
    if (rc < 0) __builtin_trap();
    return h;
}

void __asc_wasi_thread_join(struct asc_wasi_task *h) {
    // Spin via volatile read. memory.atomic.wait32 trips an alignment
    // check in wasmtime on some struct layouts even when the effective
    // address is 4-byte aligned, so use a plain volatile load for now.
    volatile int32_t *done = (volatile int32_t *)&h->done_flag;
    while (*done == 0) {
        // busy-spin
    }
    free(h);
}

#else

#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

struct asc_wasi_task {
    int32_t tid;
    int32_t done_flag;
    // Matches the HIRBuilder-synthesized wrapper signature `ptr(ptr)` so
    // wasm call_indirect type-checks; return value ignored on both paths.
    void *(*entry)(void *);
    void *arg;
};

struct asc_wasi_task_host {
    struct asc_wasi_task base;
    pthread_t tid;
};

static void *__asc_wasi_thread_trampoline(void *p) {
    struct asc_wasi_task *h = (struct asc_wasi_task *)p;
    (void)h->entry(h->arg);
    __atomic_store_n(&h->done_flag, 1, __ATOMIC_RELEASE);
    return NULL;
}

struct asc_wasi_task *__asc_wasi_thread_spawn(void *(*entry)(void *), void *arg) {
    struct asc_wasi_task_host *h =
        (struct asc_wasi_task_host *)malloc(sizeof(*h));
    if (!h) return NULL;
    h->base.tid = 0;
    h->base.done_flag = 0;
    h->base.entry = entry;
    h->base.arg = arg;
    if (pthread_create(&h->tid, NULL, __asc_wasi_thread_trampoline, h) != 0) {
        free(h);
        return NULL;
    }
    return (struct asc_wasi_task *)h;
}

void __asc_wasi_thread_join(struct asc_wasi_task *h) {
    struct asc_wasi_task_host *hh = (struct asc_wasi_task_host *)h;
    pthread_join(hh->tid, NULL);
    free(hh);
}

#endif
