// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// Confirm Inner_default IS synthesized (all i32 fields).
// RUN: grep -q "sym_name = \"Inner_default\"" %t.out
// Outer_default should NOT be synthesized (Inner field is non-primitive).
// RUN: ! grep -q "sym_name = \"Outer_default\"" %t.out

@derive(Default)
struct Inner { x: i32 }

@derive(Default)
struct Outer { inner: Inner, n: i32 }

function main(): i32 {
  let i = Inner::default();
  return i.x;
}
