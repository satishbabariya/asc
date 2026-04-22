// RUN: %asc build %s --target wasm32-wasi-threads --emit llvmir -o - | FileCheck %s --implicit-check-not='call {{.*}} @pthread_create' --implicit-check-not='call {{.*}} @pthread_join'
// CHECK-DAG: call {{.*}} @__asc_wasi_thread_spawn
// CHECK-DAG: call void @__asc_wasi_thread_join
function worker(): void {
    return;
}
function main(): i32 {
    let h = task.spawn(worker);
    task.join(h);
    return 0;
}
