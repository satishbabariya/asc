// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "__asc_top_level_panic_handler" %t.out
// Test: main function gets top-level panic handler declared.

function main(): i32 {
  return 0;
}
