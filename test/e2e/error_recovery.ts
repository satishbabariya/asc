// Test: error recovery — multiple errors in one file.
// Expected: parser reports multiple errors, does not crash.

function main(): i32 {
  let x = ;
  return 42;
}
