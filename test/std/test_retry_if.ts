// RUN: %asc check %s
// Test: retry_if with custom predicate (std/async/retry.ts).
// The retry_if function uses a predicate to decide whether to retry each error.
// This placeholder validates that the file compiles; retry_if is a std free function.
function main(): i32 {
  return 0;
}
