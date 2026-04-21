// RUN: %asc build %s --target wasm32-wasi-threads --emit llvmir -o - | FileCheck %s
// CHECK-NOT: call i32 @pthread_create
// CHECK: call {{.*}} @__asc_wasi_thread_spawn
function worker(): void {
    return;
}
function main(): i32 {
    task.spawn(worker);
    return 0;
}
