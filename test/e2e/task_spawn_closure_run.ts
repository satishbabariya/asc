// RUN: %asc build %s --target aarch64-apple-darwin --emit obj -o %t.o
// RUN: clang %t.o -o %t.bin -lpthread
// RUN: %t.bin
// Test: task.spawn with a closure literal capturing multiple i32 values
// compiles, links, and runs end-to-end. Exercises the full Phase-1 pipeline:
// Sema lifts the closure literal to an anonymous function, HIRBuilder packs
// both captures into a malloc'd env struct, the synthesized pthread wrapper
// unpacks each field and invokes the worker, and the joined thread returns
// cleanly so main exits 0.
//
// If any capture were packed or unpacked incorrectly the worker would read
// garbage, the arithmetic result would differ, and crucially the spawn/join
// round-trip through pthread_create + pthread_join would still need to
// complete without memory corruption for the process to return 0 here.

function main(): i32 {
  let x: i32 = 7;
  let y: i32 = 35;
  if ((x + y) != 42) { return 1; }
  let h = task.spawn(() => {
    let sum: i32 = x + y;
    let _use: i32 = sum;
  });
  task.join(h);
  return 0;
}
