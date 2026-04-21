// RUN: %asc build %s --target wasm32-wasi-threads -o %t.wasm --verbose 2>&1 | FileCheck %s
// CHECK: [link]
// CHECK-SAME: --shared-memory
// CHECK-SAME: --import-memory
// CHECK-SAME: --max-memory=
// CHECK-SAME: --export=wasi_thread_start
function main(): i32 { return 0; }
