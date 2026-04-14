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

// Drop flag operations for conditional moves (RFC-0008).
// These track at runtime whether a value has been moved, gating the drop.

class OwnDropFlagAllocOp
    : public mlir::Op<OwnDropFlagAllocOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::ZeroOperands> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.drop_flag_alloc"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Type resultType);
  mlir::Value getResult() { return getOperation()->getResult(0); }
};

class OwnDropFlagSetOp
    : public mlir::Op<OwnDropFlagSetOp, mlir::OpTrait::ZeroResults,
                       mlir::OpTrait::NOperands<2>::Impl> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.drop_flag_set"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value flag, mlir::Value boolValue);
  mlir::Value getFlag() { return getOperand(0); }
  mlir::Value getBoolValue() { return getOperand(1); }
};

class OwnDropFlagCheckOp
    : public mlir::Op<OwnDropFlagCheckOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.drop_flag_check"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value flag);
  mlir::Value getFlag() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }
};

class BorrowRefOp
    : public mlir::Op<BorrowRefOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "own.borrow_ref"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() {
    static llvm::StringRef names[] = {"regionId"};
    return names;
  }

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
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() {
    static llvm::StringRef names[] = {"regionId"};
    return names;
  }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value ownedValue);
  mlir::Value getOwnedValue() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }
};

} // namespace own
} // namespace asc

#endif // ASC_HIR_OWNOPS_H
