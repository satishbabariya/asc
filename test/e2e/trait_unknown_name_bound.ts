// RUN: not %asc check %s 2>&1 | grep -q "unknown trait"
// Test: generic bound with an unknown trait is rejected.

function process<T: BogusTraitThatDoesNotExist>(x: T): i32 {
  return 0;
}

function main(): i32 { return 0; }
