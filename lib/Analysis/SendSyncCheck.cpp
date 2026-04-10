// SendSyncCheck — verifies Send/Sync constraints on task.spawn captures.
//
// Ensures that values captured by task.spawn satisfy the Send trait and
// that shared references across threads satisfy Sync.

#include "asc/Analysis/SendSyncCheck.h"
#include "asc/HIR/OwnTypes.h"
#include "asc/HIR/TaskOps.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

namespace asc {

//===----------------------------------------------------------------------===//
// Type classification helpers
//===----------------------------------------------------------------------===//

bool SendSyncCheckPass::isPrimitiveType(mlir::Type type) const {
  // Integer and float types are always Send + Sync.
  if (type.isIntOrIndexOrFloat())
    return true;

  // i1 (bool) is also primitive.
  if (auto intType = type.dyn_cast<mlir::IntegerType>())
    return true;

  return false;
}

bool SendSyncCheckPass::isSendType(mlir::Type type) const {
  // Primitives are always Send.
  if (isPrimitiveType(type))
    return true;

  // OwnValType: check the Send flag from type parameters.
  if (auto ownVal = mlir::dyn_cast<own::OwnValType>(type))
    return ownVal.isSend();

  // BorrowType (shared): Send if inner type is Sync.
  if (auto borrow = mlir::dyn_cast<own::BorrowType>(type))
    return isSyncType(borrow.getInnerType());

  // BorrowMutType: NEVER Send (RFC-0006 Pass 5).
  if (mlir::isa<own::BorrowMutType>(type))
    return false;

  // Channel types are Send.
  if (mlir::isa<task::ChanTxType>(type) || mlir::isa<task::ChanRxType>(type))
    return true;

  // Task handles are Send.
  if (mlir::isa<task::TaskHandleType>(type))
    return true;

  // Function types are Send.
  if (mlir::isa<mlir::FunctionType>(type))
    return true;

  // LLVM pointer types — raw pointers are NOT Send by default.
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(type))
    return false;

  // Default: conservatively assume not Send.
  return false;
}

bool SendSyncCheckPass::isSyncType(mlir::Type type) const {
  // Primitives are always Sync.
  if (isPrimitiveType(type))
    return true;

  // OwnValType: Sync if marked Sync.
  if (auto ownVal = mlir::dyn_cast<own::OwnValType>(type))
    return ownVal.isSync();

  // Shared borrows are Sync if inner type is Sync.
  if (auto borrow = mlir::dyn_cast<own::BorrowType>(type))
    return isSyncType(borrow.getInnerType());

  // Mutable borrows are NOT Sync.
  if (mlir::isa<own::BorrowMutType>(type))
    return false;

  // Function types are Sync.
  if (mlir::isa<mlir::FunctionType>(type))
    return true;

  // Default: conservatively not Sync.
  return false;
}

//===----------------------------------------------------------------------===//
// Checking logic
//===----------------------------------------------------------------------===//

void SendSyncCheckPass::checkSpawnOps(mlir::func::FuncOp func) {
  func.walk([&](mlir::Operation *op) {
    llvm::StringRef opName = op->getName().getStringRef();
    if (opName != "task.spawn")
      return;

    // task.spawn captures values as operands. Each captured value must
    // be Send. Shared borrows additionally require the referent to be Sync.
    for (mlir::Value operand : op->getOperands()) {
      mlir::Type type = operand.getType();

      // Check Send.
      if (!isSendType(type)) {
        reportNotSend(op, operand, "task.spawn capture");
        continue;
      }

      // For shared borrows, check Sync on the referent.
      if (mlir::isa<asc::own::BorrowType>(type)) {
        if (!isSyncType(type)) {
          reportNotSync(op, operand, "shared borrow in task.spawn");
        }
      }

      // Mutable borrows should never reach task.spawn (caught by AliasCheck),
      // but double-check here for defense in depth.
      if (mlir::isa<asc::own::BorrowMutType>(type)) {
        mlir::InFlightDiagnostic diag = op->emitError()
            << "mutable borrow cannot be captured by task.spawn; "
            << "exclusive borrows cannot be shared across threads";
        if (auto *defOp = operand.getDefiningOp()) {
          diag.attachNote(defOp->getLoc())
              << "mutable borrow created here";
        }
        signalPassFailure();
      }
    }
  });
}

void SendSyncCheckPass::checkChannelSends(mlir::func::FuncOp func) {
  func.walk([&](mlir::Operation *op) {
    llvm::StringRef opName = op->getName().getStringRef();
    if (opName != "chan.send")
      return;

    // chan.send's first operand is the channel, second is the value.
    if (op->getNumOperands() < 2)
      return;

    mlir::Value sentValue = op->getOperand(1);
    mlir::Type type = sentValue.getType();

    if (!isSendType(type)) {
      reportNotSend(op, sentValue, "channel send");
    }
  });
}

void SendSyncCheckPass::reportNotSend(mlir::Operation *op,
                                       mlir::Value value,
                                       llvm::StringRef context) {
  mlir::InFlightDiagnostic diag = op->emitError()
      << "value does not satisfy Send trait, required for " << context;
  if (auto *defOp = value.getDefiningOp()) {
    diag.attachNote(defOp->getLoc()) << "non-Send value defined here";
  }
  diag.attachNote()
      << "Send is required because the value crosses a thread boundary";
  signalPassFailure();
}

void SendSyncCheckPass::reportNotSync(mlir::Operation *op,
                                       mlir::Value value,
                                       llvm::StringRef context) {
  mlir::InFlightDiagnostic diag = op->emitError()
      << "referenced type does not satisfy Sync trait, required for "
      << context;
  if (auto *defOp = value.getDefiningOp()) {
    diag.attachNote(defOp->getLoc()) << "non-Sync reference defined here";
  }
  diag.attachNote()
      << "Sync is required because the reference is shared across threads";
  signalPassFailure();
}

void SendSyncCheckPass::runOnOperation() {
  mlir::func::FuncOp func = getOperation();
  if (func.isDeclaration())
    return;

  // Check all task.spawn captures for Send (and Sync where needed).
  checkSpawnOps(func);

  // Check all channel send operations for Send.
  checkChannelSends(func);
}

std::unique_ptr<mlir::Pass> createSendSyncCheckPass() {
  return std::make_unique<SendSyncCheckPass>();
}

} // namespace asc
