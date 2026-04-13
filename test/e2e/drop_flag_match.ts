// RUN: %asc build %s --emit mlir > %t.out 2>&1; grep -q "own.drop_flag_alloc" %t.out
// Test: match arm conditional move with drop flag.

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
