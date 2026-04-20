#ifndef ASC_RUNTIME_WASI_THREAD_RT_H
#define ASC_RUNTIME_WASI_THREAD_RT_H

struct asc_wasi_task;

struct asc_wasi_task *__asc_wasi_thread_spawn(void (*entry)(void *), void *arg);
void __asc_wasi_thread_join(struct asc_wasi_task *h);

#endif
