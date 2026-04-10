// RUN: %asc check %s
// Test: deep call chain through 5 functions.

function a(x: i32): i32 { return b(x + 1); }
function b(x: i32): i32 { return c(x + 1); }
function c(x: i32): i32 { return d(x + 1); }
function d(x: i32): i32 { return e(x + 1); }
function e(x: i32): i32 { return x + 1; }

function main(): i32 {
  return a(0);
}
// Expected: exit 5
