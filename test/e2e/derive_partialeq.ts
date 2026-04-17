// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Color_eq\"" %t.out
// RUN: grep -q "func.call.*Color_eq" %t.out

@derive(PartialEq)
struct Color { r: i32, g: i32, b: i32 }

function main(): i32 {
  let a = Color { r: 1, g: 2, b: 3 };
  let b = Color { r: 1, g: 2, b: 3 };
  if a.eq(&b) { return 0; }
  return 1;
}
