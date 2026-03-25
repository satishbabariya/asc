#ifndef ASC_ANALYSIS_SENDSYNCCHECK_H
#define ASC_ANALYSIS_SENDSYNCCHECK_H

#include "mlir/IR/Value.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/DenseSet.h"
#include <memory>

namespace asc {

/// Send/Sync checking pass — verifies concurrency safety of task.spawn.
///
/// When a task.spawn operation captures values from the enclosing scope,
/// those values must satisfy the Send trait (transferable across threads).
/// When values are shared across concurrent tasks via channels, the
/// underlying type must satisfy Sync.
///
/// Rules:
/// 1. All captured values in task.spawn must be Send.
/// 2. Values sent through chan.send must be Send.
/// 3. Shared references (&T) passed to task.spawn require T: Sync.
/// 4. Mutable references (&mut T) cannot be captured by task.spawn at all
///    (this is already prevented by AliasCheck, but we double-check here).
///
/// Send types: primitives, own<T> where T: Send, tuples of Send types.
/// Non-Send types: raw pointers, !own.val with interior mutability.
/// Sync types: primitives, immutable references, types behind atomics.
class SendSyncCheckPass
    : public mlir::PassWrapper<SendSyncCheckPass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(SendSyncCheckPass)

  llvm::StringRef getArgument() const override {
    return "asc-send-sync-check";
  }
  llvm::StringRef getDescription() const override {
    return "Verify Send/Sync constraints on task.spawn captures";
  }

  void runOnOperation() override;

private:
  /// Check all task.spawn operations in the function.
  void checkSpawnOps(mlir::func::FuncOp func);

  /// Check all channel send operations in the function.
  void checkChannelSends(mlir::func::FuncOp func);

  /// Determine if a type satisfies the Send trait.
  bool isSendType(mlir::Type type) const;

  /// Determine if a type satisfies the Sync trait.
  bool isSyncType(mlir::Type type) const;

  /// Check if a type is a primitive (always Send + Sync).
  bool isPrimitiveType(mlir::Type type) const;

  /// Report a Send violation.
  void reportNotSend(mlir::Operation *op, mlir::Value value,
                     llvm::StringRef context);

  /// Report a Sync violation.
  void reportNotSync(mlir::Operation *op, mlir::Value value,
                     llvm::StringRef context);
};

/// Create a Send/Sync check pass.
std::unique_ptr<mlir::Pass> createSendSyncCheckPass();

} // namespace asc

#endif // ASC_ANALYSIS_SENDSYNCCHECK_H
