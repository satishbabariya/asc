// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Counter_default\"" %t.out
// RUN: grep -q "func.call.*Counter_default" %t.out

@derive(Default)
struct Counter { n: i32, flag: bool }

function main(): i32 {
  let c = Counter::default();
  if c.n != 0 { return 1; }
  if c.flag { return 2; }
  return 0;
}
