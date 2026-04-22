// REQUIRES: wasmtime-threads
// RUN: %asc build %s --target wasm32-wasi-threads -o %t.wasm
// RUN: wasmtime run -W threads=y -W shared-memory=y -S threads=y %t.wasm 2>&1 | FileCheck %s
// CHECK: worker ran
// CHECK: joined
//
// End-to-end: spawn a worker on wasmtime with WASI threads, join it, then
// verify both the worker-side and main-side output reached stdout. Exercises
// wasi_thread_start + __asc_wasi_thread_spawn + __asc_wasi_thread_join
// against a real embedder. Gated on the wasmtime-threads lit feature so
// hosts without a new-enough wasmtime skip cleanly.
function worker(): void {
    println!("worker ran");
}
function main(): i32 {
    let h = task.spawn(worker);
    task.join(h);
    println!("joined");
    return 0;
}
