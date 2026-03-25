// Simplest possible program: function returning a constant.
// This is the primary end-to-end verification test.
// Pipeline: Source → Lexer → Parser → AST → Sema → HIR → BorrowCheck →
//           DropInsert → PanicWrap → Lowering → CodeGen → Output

function main(): i32 {
  const x: i32 = 42;
  return x;
}
