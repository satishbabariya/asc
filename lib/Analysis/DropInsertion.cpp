// DropInsertion — inserts own.drop operations at scope exits.
//
// Transform pass that inserts explicit drop operations for owned values
// going out of scope. Drops are inserted in reverse declaration order (LIFO).

#include "asc/Analysis/DropInsertion.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// Helper: Check if a type is an ownership type.
//===----------------------------------------------------------------------===//

static bool isOwnedType(mlir::Type type) {
  llvm::StringRef typeName = type.getAbstractType().getName();
  return typeName.contains("own.val");
}

/// Check if an operation consumes its operand (move/drop/return).
static bool isConsumingOp(mlir::Operation *op) {
  llvm::StringRef opName = op->getName().getStringRef();
  return opName == "own.move" || opName == "own.drop" ||
         opName == "own.store" || opName == "func.return" ||
         opName == "chan.send";
}

//===----------------------------------------------------------------------===//
// DropInsertionPass
//===----------------------------------------------------------------------===//

void DropInsertionPass::collectOwnedValues(mlir::func::FuncOp func) {
  ownedValuesByBlock.clear();
  nextDeclOrder = 0;

  // Collect function arguments that are owned.
  mlir::Block &entryBlock = func.getBody().front();
  for (mlir::Value arg : entryBlock.getArguments()) {
    if (isOwnedType(arg.getType())) {
      OwnedValueInfo info;
      info.value = arg;
      info.defOp = nullptr; // Function argument, no defining op.
      info.declOrder = nextDeclOrder++;
      ownedValuesByBlock[&entryBlock].push_back(info);
    }
  }

  // Collect results of operations that produce owned values.
  func.walk([&](mlir::Operation *op) {
    for (mlir::Value result : op->getResults()) {
      if (isOwnedType(result.getType())) {
        OwnedValueInfo info;
        info.value = result;
        info.defOp = op;
        info.declOrder = nextDeclOrder++;
        ownedValuesByBlock[op->getBlock()].push_back(info);
      }
    }
  });
}

bool DropInsertionPass::isConsumedBefore(mlir::Value value,
                                          mlir::Operation *point) const {
  // Check if the value is consumed by any operation before 'point'
  // in the same block.
  for (mlir::OpOperand &use : value.getUses()) {
    mlir::Operation *useOp = use.getOwner();
    if (useOp->getBlock() != point->getBlock())
      continue;
    if (isConsumingOp(useOp) && useOp->isBeforeInBlock(point))
      return true;
  }
  return false;
}

bool DropInsertionPass::hasCustomDrop(mlir::Value value) const {
  // Check if the value's type has a custom Drop implementation.
  // This is determined by checking if a __drop function exists for the type.
  // For now, we conservatively assume all non-primitive owned types have drop.
  mlir::Type type = value.getType();
  return !type.isIntOrIndexOrFloat();
}

void DropInsertionPass::insertDropBefore(mlir::Operation *insertPoint,
                                          mlir::Value value) {
  mlir::OpBuilder builder(insertPoint);
  mlir::Location loc = insertPoint->getLoc();

  // Create an own.drop operation. The operation name is "own.drop" in
  // our custom dialect.
  mlir::OperationState state(loc, "own.drop");
  state.addOperands(value);
  builder.create(state);
}

void DropInsertionPass::insertBlockExitDrops(mlir::Block &block) {
  auto it = ownedValuesByBlock.find(&block);
  if (it == ownedValuesByBlock.end())
    return;

  auto &ownedValues = it->second;
  if (ownedValues.empty())
    return;

  // The terminator is the insertion point for drops at block exit.
  mlir::Operation *terminator = block.getTerminator();
  if (!terminator)
    return;

  // Don't insert drops before return — returnDrops handles that.
  llvm::StringRef termName = terminator->getName().getStringRef();
  if (termName == "func.return")
    return;

  // Sort by declaration order descending (LIFO drop order).
  llvm::SmallVector<OwnedValueInfo, 8> sorted(ownedValues.begin(),
                                                ownedValues.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const OwnedValueInfo &a, const OwnedValueInfo &b) {
              return a.declOrder > b.declOrder;
            });

  // Insert drops for values that haven't been consumed.
  for (const auto &info : sorted) {
    if (!isConsumedBefore(info.value, terminator)) {
      insertDropBefore(terminator, info.value);
    }
  }
}

void DropInsertionPass::insertReturnDrops(mlir::func::FuncOp func) {
  // For each return operation, drop all owned values that are not being
  // returned.
  func.walk([&](mlir::Operation *op) {
    llvm::StringRef opName = op->getName().getStringRef();
    if (opName != "func.return")
      return;

    // Collect the set of values being returned.
    llvm::DenseSet<mlir::Value> returnedValues;
    for (mlir::Value operand : op->getOperands()) {
      returnedValues.insert(operand);
    }

    // For each block that dominates the return, collect owned values
    // that need dropping. We only need to check the return's own block
    // and values that are live-in to it.
    mlir::Block *block = op->getBlock();

    // Collect all owned values visible at this return point.
    // This includes values defined in this block and values live-in.
    llvm::SmallVector<OwnedValueInfo, 16> toDrop;

    // Gather from all blocks (values that are live at this return).
    for (auto &[blk, values] : ownedValuesByBlock) {
      for (const auto &info : values) {
        if (returnedValues.count(info.value))
          continue; // Being returned, don't drop.
        if (isConsumedBefore(info.value, op))
          continue; // Already consumed.

        // Check if the value is still live at this return.
        // Simplified: if defined in the same block or is a function arg,
        // and not consumed, it needs dropping.
        if (blk == block || info.defOp == nullptr) {
          toDrop.push_back(info);
        }
      }
    }

    // Sort by declaration order descending (LIFO).
    std::sort(toDrop.begin(), toDrop.end(),
              [](const OwnedValueInfo &a, const OwnedValueInfo &b) {
                return a.declOrder > b.declOrder;
              });

    for (const auto &info : toDrop) {
      insertDropBefore(op, info.value);
    }
  });
}

void DropInsertionPass::insertEarlyExitDrops(mlir::func::FuncOp func) {
  // For loop break/continue operations, drop values that would be
  // skipped over.
  func.walk([&](mlir::Operation *op) {
    llvm::StringRef opName = op->getName().getStringRef();

    // Detect early exit operations (break, continue).
    bool isEarlyExit = opName == "own.break" || opName == "own.continue";
    if (!isEarlyExit)
      return;

    mlir::Block *block = op->getBlock();
    auto it = ownedValuesByBlock.find(block);
    if (it == ownedValuesByBlock.end())
      return;

    // Drop all owned values in the current scope that haven't been consumed.
    llvm::SmallVector<OwnedValueInfo, 8> toDrop;
    for (const auto &info : it->second) {
      if (!isConsumedBefore(info.value, op)) {
        toDrop.push_back(info);
      }
    }

    // LIFO order.
    std::sort(toDrop.begin(), toDrop.end(),
              [](const OwnedValueInfo &a, const OwnedValueInfo &b) {
                return a.declOrder > b.declOrder;
              });

    for (const auto &info : toDrop) {
      insertDropBefore(op, info.value);
    }
  });
}

void DropInsertionPass::runOnOperation() {
  mlir::func::FuncOp func = getOperation();
  if (func.isDeclaration())
    return;

  // Step 1: Collect all owned values, indexed by block.
  collectOwnedValues(func);

  // Step 2: Insert drops at return statements (non-returned owned values).
  insertReturnDrops(func);

  // Step 3: Insert drops at early exits (break, continue).
  insertEarlyExitDrops(func);

  // Step 4: Insert drops at block exits for values going out of scope.
  for (auto &block : func.getBody()) {
    insertBlockExitDrops(block);
  }
}

std::unique_ptr<mlir::Pass> createDropInsertionPass() {
  return std::make_unique<DropInsertionPass>();
}

} // namespace asc
