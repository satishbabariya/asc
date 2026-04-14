// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "__asc_chan_drop" %t.out
// Test: channel values get ref-counted drop.

function main(): i32 {
  let ch = chan<i32>(4);
  return 0;
}
