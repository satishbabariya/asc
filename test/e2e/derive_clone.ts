// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Counter_clone\"" %t.out
// RUN: grep -q "func.call.*Counter_clone" %t.out

@derive(Clone)
struct Counter { n: i32 }

function main(): i32 {
  let p = Counter { n: 42 };
  let q = p.clone();
  return q.n;
}
