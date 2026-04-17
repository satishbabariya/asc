// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Empty_clone\"" %t.out
// RUN: grep -q "sym_name = \"Empty_eq\"" %t.out
// RUN: grep -q "sym_name = \"Empty_default\"" %t.out

@derive(Clone, PartialEq, Default)
struct Empty {}

function main(): i32 {
  let a = Empty {};
  let b = a.clone();
  let c = Empty::default();
  if a.eq(&b) { return 0; }
  return 1;
}
