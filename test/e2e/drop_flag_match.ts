// RUN: %asc build %s --emit llvmir > %t.out 2>&1
// RUN: grep -q "br i1" %t.out
// Test: match arm conditional move with conditional branch around drop.

struct Handle { id: i32 }
function take(h: own<Handle>): void { }
function main(): i32 {
  let h = Handle { id: 1 };
  let choice: i32 = 1;
  match choice {
    1 => take(h),
    _ => { }
  }
  return 0;
}
