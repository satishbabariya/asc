// RUN: %asc build %s --emit llvmir > %t.out 2>&1
// RUN: grep -q "br i1" %t.out
// Test: conditional move inserts drop flag with conditional branch around drop.

struct Resource { id: i32 }
function consume(r: own<Resource>): void { }
function main(): i32 {
  let r = Resource { id: 42 };
  let flag: bool = true;
  if flag {
    consume(r);
  }
  return 0;
}
