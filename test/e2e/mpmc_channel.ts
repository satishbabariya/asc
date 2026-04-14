// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "__asc_mpmc_chan_create" %t.out
// Test: MPMC channel runtime functions are declared (native target only, pthreads).

function main(): i32 {
  return 0;
}
