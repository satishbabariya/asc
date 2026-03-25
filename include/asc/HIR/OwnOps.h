#ifndef ASC_HIR_OWNOPS_H
#define ASC_HIR_OWNOPS_H

#include "asc/HIR/OwnDialect.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

namespace asc {
namespace own {

//===----------------------------------------------------------------------===//
// OwnAllocOp: own.alloc
//
// Allocates a new owned value. The result is an !own.val<T>.
//   %val = own.alloc %init : !own.val<i32>
//===----------------------------------------------------------------------===//
class OwnAllocOp
    : public mlir::Op<OwnAllocOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand,
                       mlir::MemoryEffectOpInterface::Trait> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.alloc"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Type resultType, mlir::Value initValue);

  /// The initial value to store.
  mlir::Value getInitValue() { return getOperand(); }

  /// The result owned value.
  mlir::Value getResult() { return getOperation()->getResult(0); }

  /// Memory effects: allocates.
  void getEffects(
      llvm::SmallVectorImpl<mlir::SideEffects::EffectInstance<
          mlir::MemoryEffects::Effect>> &effects);

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// OwnMoveOp: own.move
//
// Transfers ownership of a value. Invalidates the source.
//   %new = own.move %old : !own.val<i32>
//===----------------------------------------------------------------------===//
class OwnMoveOp
    : public mlir::Op<OwnMoveOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.move"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value source);

  mlir::Value getSource() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// OwnDropOp: own.drop
//
// Destroys an owned value, releasing its resources.
//   own.drop %val : !own.val<i32>
//===----------------------------------------------------------------------===//
class OwnDropOp
    : public mlir::Op<OwnDropOp, mlir::OpTrait::ZeroResults,
                       mlir::OpTrait::OneOperand,
                       mlir::MemoryEffectOpInterface::Trait> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.drop"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value ownedValue);

  mlir::Value getOwnedValue() { return getOperand(); }

  void getEffects(
      llvm::SmallVectorImpl<mlir::SideEffects::EffectInstance<
          mlir::MemoryEffects::Effect>> &effects);

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// OwnCopyOp: own.copy
//
// Creates a deep copy of a value (only valid for Copy types).
//   %copy = own.copy %val : !own.val<i32>
//===----------------------------------------------------------------------===//
class OwnCopyOp
    : public mlir::Op<OwnCopyOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.copy"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value source);

  mlir::Value getSource() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// BorrowRefOp: own.borrow_ref
//
// Creates a shared (immutable) borrow from an owned value.
//   %ref = own.borrow_ref %val : !own.val<i32> -> !own.borrow<i32>
//===----------------------------------------------------------------------===//
class BorrowRefOp
    : public mlir::Op<BorrowRefOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.borrow_ref"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value ownedValue);

  mlir::Value getOwnedValue() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// BorrowMutOp: own.borrow_mut
//
// Creates an exclusive (mutable) borrow from an owned value.
//   %ref = own.borrow_mut %val : !own.val<i32> -> !own.borrow_mut<i32>
//===----------------------------------------------------------------------===//
class BorrowMutOp
    : public mlir::Op<BorrowMutOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.borrow_mut"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value ownedValue);

  mlir::Value getOwnedValue() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

} // namespace own
} // namespace asc

#endif // ASC_HIR_OWNOPS_H
