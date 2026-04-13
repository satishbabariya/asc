// RUN: %asc build %s --emit mlir > %t.out 2>&1; grep -q "own.drop_flag_alloc" %t.out
// Test: conditional move inserts drop flag.

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
