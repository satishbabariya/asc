#ifndef ASC_HIR_TASKOPS_H
#define ASC_HIR_TASKOPS_H

#include "asc/HIR/TaskDialect.h"
#include "asc/HIR/OwnTypes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/FunctionInterfaces.h"

namespace asc {
namespace task {

//===----------------------------------------------------------------------===//
// TaskHandleType: !task.handle
//
// An opaque handle to a spawned task. Used to join and retrieve the result.
//===----------------------------------------------------------------------===//
class TaskHandleType
    : public mlir::Type::TypeBase<TaskHandleType, mlir::Type,
                                  mlir::TypeStorage> {
public:
  struct Storage : public mlir::TypeStorage {
    Storage(mlir::Type resultType) : resultType(resultType) {}

    using KeyTy = mlir::Type;

    bool operator==(const KeyTy &key) const { return key == resultType; }

    static llvm::hash_code hashKey(const KeyTy &key) {
      return llvm::hash_value(key);
    }

    static Storage *construct(mlir::TypeStorageAllocator &allocator,
                              const KeyTy &key) {
      return new (allocator.allocate<Storage>()) Storage(key);
    }

    mlir::Type resultType;
  };

  using Base = mlir::Type::TypeBase<TaskHandleType, mlir::Type, Storage>;
  using Base::Base;

  /// Get a TaskHandleType for a task that returns the given type.
  static TaskHandleType get(mlir::MLIRContext *ctx, mlir::Type resultType);

  /// The type the task will produce when joined.
  mlir::Type getResultType() const;
};

//===----------------------------------------------------------------------===//
// ChanTxType: !task.chan_tx<element_type>
//
// The sending half of a channel.
//===----------------------------------------------------------------------===//
class ChanTxType : public mlir::Type::TypeBase<ChanTxType, mlir::Type,
                                                mlir::TypeStorage> {
public:
  struct Storage : public mlir::TypeStorage {
    Storage(mlir::Type elementType) : elementType(elementType) {}

    using KeyTy = mlir::Type;

    bool operator==(const KeyTy &key) const { return key == elementType; }

    static llvm::hash_code hashKey(const KeyTy &key) {
      return llvm::hash_value(key);
    }

    static Storage *construct(mlir::TypeStorageAllocator &allocator,
                              const KeyTy &key) {
      return new (allocator.allocate<Storage>()) Storage(key);
    }

    mlir::Type elementType;
  };

  using Base = mlir::Type::TypeBase<ChanTxType, mlir::Type, Storage>;
  using Base::Base;

  /// Get a ChanTxType for the given element type.
  static ChanTxType get(mlir::MLIRContext *ctx, mlir::Type elementType);

  /// The element type transported by this channel.
  mlir::Type getElementType() const;
};

//===----------------------------------------------------------------------===//
// ChanRxType: !task.chan_rx<element_type>
//
// The receiving half of a channel.
//===----------------------------------------------------------------------===//
class ChanRxType : public mlir::Type::TypeBase<ChanRxType, mlir::Type,
                                                mlir::TypeStorage> {
public:
  struct Storage : public mlir::TypeStorage {
    Storage(mlir::Type elementType) : elementType(elementType) {}

    using KeyTy = mlir::Type;

    bool operator==(const KeyTy &key) const { return key == elementType; }

    static llvm::hash_code hashKey(const KeyTy &key) {
      return llvm::hash_value(key);
    }

    static Storage *construct(mlir::TypeStorageAllocator &allocator,
                              const KeyTy &key) {
      return new (allocator.allocate<Storage>()) Storage(key);
    }

    mlir::Type elementType;
  };

  using Base = mlir::Type::TypeBase<ChanRxType, mlir::Type, Storage>;
  using Base::Base;

  /// Get a ChanRxType for the given element type.
  static ChanRxType get(mlir::MLIRContext *ctx, mlir::Type elementType);

  /// The element type transported by this channel.
  mlir::Type getElementType() const;
};

//===----------------------------------------------------------------------===//
// TaskSpawnOp: task.spawn
//
// Spawns a new task executing the given callee. The callee must accept only
// Send arguments. Returns a !task.handle<T>.
//   %handle = task.spawn @callee(%arg0, %arg1) : (i32, i64) -> !task.handle<i32>
//===----------------------------------------------------------------------===//
class TaskSpawnOp
    : public mlir::Op<TaskSpawnOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::VariadicOperands> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "task.spawn"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Type handleType, mlir::FlatSymbolRefAttr callee,
                    mlir::ValueRange args);

  /// The callee function symbol.
  mlir::FlatSymbolRefAttr getCalleeAttr();
  llvm::StringRef getCallee();

  /// Arguments passed to the spawned function.
  mlir::Operation::operand_range getArgOperands() {
    return getOperands();
  }

  mlir::Value getResult() { return getOperation()->getResult(0); }

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// TaskJoinOp: task.join
//
// Blocks until the task completes and retrieves its result.
//   %result = task.join %handle : !task.handle<i32> -> i32
//===----------------------------------------------------------------------===//
class TaskJoinOp
    : public mlir::Op<TaskJoinOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "task.join"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value handle);

  mlir::Value getHandle() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// ChanMakeOp: task.chan_make
//
// Creates a bounded channel, returning (tx, rx) pair.
//   %tx, %rx = task.chan_make {capacity = 8} : !task.chan_tx<i32>, !task.chan_rx<i32>
//===----------------------------------------------------------------------===//
class ChanMakeOp
    : public mlir::Op<ChanMakeOp, mlir::OpTrait::NResults<2>::Impl,
                       mlir::OpTrait::ZeroOperands> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "task.chan_make"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Type elementType, uint64_t capacity);

  /// The sending end of the channel.
  mlir::Value getTx() { return getOperation()->getResult(0); }

  /// The receiving end of the channel.
  mlir::Value getRx() { return getOperation()->getResult(1); }

  /// Channel buffer capacity.
  uint64_t getCapacity();

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// ChanSendOp: task.chan_send
//
// Sends an owned value through a channel. Ownership is transferred.
//   task.chan_send %tx, %val : !task.chan_tx<i32>, !own.val<i32>
//===----------------------------------------------------------------------===//
class ChanSendOp
    : public mlir::Op<ChanSendOp, mlir::OpTrait::ZeroResults,
                       mlir::OpTrait::NOperands<2>::Impl> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "task.chan_send"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value tx, mlir::Value value);

  mlir::Value getTx() { return getOperand(0); }
  mlir::Value getValue() { return getOperand(1); }

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

//===----------------------------------------------------------------------===//
// ChanRecvOp: task.chan_recv
//
// Receives an owned value from a channel. Blocks until a value is available.
//   %val = task.chan_recv %rx : !task.chan_rx<i32> -> !own.val<i32>
//===----------------------------------------------------------------------===//
class ChanRecvOp
    : public mlir::Op<ChanRecvOp, mlir::OpTrait::OneResult,
                       mlir::OpTrait::OneOperand> {
public:
  using Op::Op;
  static llvm::StringRef getOperationName() { return "task.chan_recv"; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::Value rx);

  mlir::Value getRx() { return getOperand(); }
  mlir::Value getResult() { return getOperation()->getResult(0); }

  static mlir::ParseResult parse(mlir::OpAsmParser &parser,
                                 mlir::OperationState &result);
  void print(mlir::OpAsmPrinter &p);

  mlir::LogicalResult verify();
};

} // namespace task
} // namespace asc

#endif // ASC_HIR_TASKOPS_H
