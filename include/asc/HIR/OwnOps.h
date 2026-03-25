#ifndef ASC_HIR_OWNOPS_H
#define ASC_HIR_OWNOPS_H

#include "asc/HIR/OwnDialect.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"

namespace asc {
namespace own {

// DECISION: Own dialect operations are created using the generic
// mlir::Operation API with string-based op names rather than
// C++ Op<> template classes. This avoids LLVM 18 compatibility
// issues with manual op registration (missing getAttributeNames,
// MemoryEffectOpInterface traits, etc.).
//
// Operations are created via:
//   builder.create<OperationState>("own.alloc", ...)
// or via helper functions below.
//
// The lowering passes match operations by name string.

// Simplified op classes for type-safe creation.
// These are thin wrappers that delegate to OperationState.

class OwnAllocOp
    : public mlir::Op<OwnAllocOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.alloc"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Type resultType, mlir::Value initValue);
  mlir::Value getInitValue() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }
};

class OwnMoveOp
    : public mlir::Op<OwnMoveOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.move"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value source);
  mlir::Value getSource() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }
};

class OwnDropOp
    : public mlir::Op<OwnDropOp, mlir::OpTrait::ZeroResults,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.drop"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value ownedValue);
  mlir::Value getOwnedValue() { return getOperand(); }
};

class OwnCopyOp
    : public mlir::Op<OwnCopyOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.copy"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value source);
};

class BorrowRefOp
    : public mlir::Op<BorrowRefOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.borrow_ref"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value ownedValue);
  mlir::Value getOwnedValue() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }
};

class BorrowMutOp
    : public mlir::Op<BorrowMutOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.borrow_mut"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value ownedValue);
  mlir::Value getOwnedValue() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }
};

} // namespace own
} // namespace asc

#endif // ASC_HIR_OWNOPS_H
