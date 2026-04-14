// RUN: %asc build %s --debug --target aarch64-apple-darwin --emit llvmir > %t.out 2>&1
// RUN: grep -q "DISubprogram\|!dbg\|debug" %t.out
// Test: --debug flag adds debug metadata to LLVM IR.

function main(): i32 {
  return 42;
}
