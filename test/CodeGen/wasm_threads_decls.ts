// RUN: %asc build %s --target wasm32-wasi-threads --emit llvmir -o - | FileCheck %s
// CHECK-DAG: declare{{.*}} ptr @__asc_wasi_thread_spawn(ptr, ptr)
// CHECK-DAG: declare{{.*}} void @__asc_wasi_thread_join(ptr)
function worker(): void {
    return;
}
function main(): i32 {
    task.spawn(worker);
    return 0;
}
