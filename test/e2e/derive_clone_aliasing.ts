// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Pair_clone\"" %t.out
// Verify clone produces independent storage by checking the call appears
// twice when we clone twice.
// RUN: grep -c "func.call.*Pair_clone" %t.out | grep -q "2"

@derive(Clone)
struct Pair { left: i32, right: i32 }

function main(): i32 {
  let p = Pair { left: 100, right: 200 };
  let q = p.clone();
  let r = p.clone();
  return q.left + r.right;
}
