#ifndef ASC_RUNTIME_WASI_THREAD_RT_H
#define ASC_RUNTIME_WASI_THREAD_RT_H

struct asc_wasi_task;

// Entry returns void* for layout compatibility with HIRBuilder's pthread-style
// wrapper (`ptr(ptr)`). Wasm call_indirect enforces the signature; the host
// shim also follows it for consistency. The return value is always ignored.
struct asc_wasi_task *__asc_wasi_thread_spawn(void *(*entry)(void *), void *arg);
void __asc_wasi_thread_join(struct asc_wasi_task *h);

#endif
