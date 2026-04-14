// RUN: %asc check %s
// Test: break from labeled loop works.

function main(): i32 {
  let x: i32 = 0;
  outer: loop {
    x = 42;
    break outer;
  }
  return x;
}
