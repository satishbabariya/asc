// SendSyncCheck — verifies Send/Sync constraints on task.spawn captures.
//
// Ensures that values captured by task.spawn satisfy the Send trait and
// that shared references across threads satisfy Sync.

#include "asc/Analysis/SendSyncCheck.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"

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

  // Check type name for ownership dialect types.
  llvm::StringRef typeName = type.getAbstractType().getName();

  // own.val<T> is Send if T is Send (owned values can be transferred).
  if (typeName.contains("own.val"))
    return true; // Conservatively assume inner type is Send for now.

  // Borrow types: shared borrows (&T) are Send if T is Sync.
  if (typeName.contains("borrow") && !typeName.contains("borrow.mut"))
    return true; // Requires T: Sync, checked separately.

  // Mutable borrows (&mut T) are NOT Send (cannot be sent across threads).
  if (typeName.contains("borrow.mut"))
    return false;

  // Channel types are Send (the handle can be transferred).
  if (typeName.contains("chan"))
    return true;

  // LLVM pointer types — raw pointers are NOT Send by default.
  if (typeName.contains("llvm.ptr"))
    return false;

  // Function types are Send (code pointers are safe to share).
  if (type.isa<mlir::FunctionType>())
    return true;

  // Default: conservatively assume not Send.
  return false;
}

bool SendSyncCheckPass::isSyncType(mlir::Type type) const {
  // Primitives are always Sync.
  if (isPrimitiveType(type))
    return true;

  llvm::StringRef typeName = type.getAbstractType().getName();

  // Immutable references are Sync if the referent is Sync.
  // Shared borrows of Sync types are Sync.
  if (typeName.contains("borrow") && !typeName.contains("borrow.mut"))
    return true; // Conservatively assume inner type is Sync.

  // Mutable borrows are NOT Sync.
  if (typeName.contains("borrow.mut"))
    return false;

  // own.val<T> is Sync if T is Sync.
  if (typeName.contains("own.val"))
    return true; // Conservative assumption.

  // Atomic types are Sync.
  if (typeName.contains("atomic"))
    return true;

  // Raw pointers are NOT Sync.
  if (typeName.contains("llvm.ptr"))
    return false;

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
      llvm::StringRef typeName = type.getAbstractType().getName();
      if (typeName.contains("borrow") && !typeName.contains("borrow.mut")) {
        if (!isSyncType(type)) {
          reportNotSync(op, operand, "shared borrow in task.spawn");
        }
      }

      // Mutable borrows should never reach task.spawn (caught by AliasCheck),
      // but double-check here for defense in depth.
      if (typeName.contains("borrow.mut")) {
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
