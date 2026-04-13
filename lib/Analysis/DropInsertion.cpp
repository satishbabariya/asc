// DropInsertion — inserts own.drop operations at scope exits.
//
// Transform pass that inserts explicit drop operations for owned values
// going out of scope. Drops are inserted in reverse declaration order (LIFO).
// For values that are conditionally moved (moved on some paths but not all),
// drop flags (i1 allocas) are emitted to guard the drop at runtime.

#include "asc/Analysis/DropInsertion.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "asc/HIR/OwnTypes.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// Helper: Check if a type is an ownership type.
//===----------------------------------------------------------------------===//

static bool isOwnedType(mlir::Type type) {
  return mlir::isa<asc::own::OwnValType>(type);
}

/// Check if an llvm.alloca allocates a struct type (owned resource).
static bool isStructAlloca(mlir::Operation *op) {
  if (auto allocaOp = mlir::dyn_cast<mlir::LLVM::AllocaOp>(op)) {
    if (auto elemType = allocaOp.getElemType()) {
      if (mlir::isa<mlir::LLVM::LLVMStructType>(elemType))
        return true;
    }
  }
  return false;
}

/// Check if an operation consumes its operand (move/drop/return/call).
static bool isConsumingOp(mlir::Operation *op) {
  llvm::StringRef opName = op->getName().getStringRef();
  return opName == "own.move" || opName == "own.drop" ||
         opName == "own.store" || opName == "func.return" ||
         opName == "chan.send" || opName == "func.call";
}

/// Follow store/load chains to find consuming uses of a struct alloca.
/// The pattern is: alloca %1 → store %1 to %ptr → load %ptr → func.call.
/// Returns true if any consuming use (func.call) exists in a different block.
static bool isConditionallyMovedThroughPtr(mlir::Value allocaVal) {
  mlir::Block *defBlock = allocaVal.getDefiningOp()->getBlock();

  // Find stores of this alloca value into pointer variables.
  for (mlir::OpOperand &use : allocaVal.getUses()) {
    auto *useOp = use.getOwner();
    if (auto storeOp = mlir::dyn_cast<mlir::LLVM::StoreOp>(useOp)) {
      // Check if the alloca value is the stored value (not the address).
      if (storeOp.getValue() == allocaVal) {
        mlir::Value ptrAddr = storeOp.getAddr();
        // Find loads from this pointer address.
        for (mlir::OpOperand &ptrUse : ptrAddr.getUses()) {
          auto *ptrUseOp = ptrUse.getOwner();
          if (auto loadOp = mlir::dyn_cast<mlir::LLVM::LoadOp>(ptrUseOp)) {
            mlir::Value loadedVal = loadOp.getResult();
            // Check if the loaded value is consumed in a different block.
            for (mlir::OpOperand &loadUse : loadedVal.getUses()) {
              auto *loadUseOp = loadUse.getOwner();
              if (isConsumingOp(loadUseOp) &&
                  loadUseOp->getBlock() != defBlock) {
                return true;
              }
            }
          }
        }
      }
    }
  }
  return false;
}

/// Check if a value has consuming uses in different blocks, indicating
/// it might be conditionally moved (moved on some paths but not others).
static bool isConditionallyMoved(mlir::Value value) {
  // For struct allocas, follow the store/load chain.
  if (auto *defOp = value.getDefiningOp()) {
    if (isStructAlloca(defOp))
      return isConditionallyMovedThroughPtr(value);
  }

  mlir::Block *defBlock = nullptr;
  if (auto *defOp = value.getDefiningOp())
    defBlock = defOp->getBlock();
  else if (auto blockArg = mlir::dyn_cast<mlir::BlockArgument>(value))
    defBlock = blockArg.getOwner();

  bool hasConsumingUse = false;
  bool hasNonDefBlockConsume = false;

  for (mlir::OpOperand &use : value.getUses()) {
    mlir::Operation *useOp = use.getOwner();
    if (isConsumingOp(useOp)) {
      hasConsumingUse = true;
      if (useOp->getBlock() != defBlock)
        hasNonDefBlockConsume = true;
    }
  }

  return hasConsumingUse && hasNonDefBlockConsume;
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

  // Also collect struct allocas that are conditionally moved — these
  // represent owned resources that need drop flag tracking.
  // Only add values that are actually conditionally moved, to avoid
  // inserting unnecessary drops for all struct allocas.
  func.walk([&](mlir::LLVM::AllocaOp allocaOp) {
    if (auto elemType = allocaOp.getElemType()) {
      if (mlir::isa<mlir::LLVM::LLVMStructType>(elemType)) {
        mlir::Value result = allocaOp.getResult();
        if (!isConditionallyMovedThroughPtr(result))
          return;
        // Avoid duplicates (if already tracked as !own.val).
        auto &vec = ownedValuesByBlock[allocaOp->getBlock()];
        bool alreadyTracked = false;
        for (const auto &info : vec) {
          if (info.value == result) {
            alreadyTracked = true;
            break;
          }
        }
        if (!alreadyTracked) {
          OwnedValueInfo info;
          info.value = result;
          info.defOp = allocaOp.getOperation();
          info.declOrder = nextDeclOrder++;
          vec.push_back(info);
        }
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

  // For struct allocas, also check through store/load chains.
  if (auto *defOp = value.getDefiningOp()) {
    if (isStructAlloca(defOp)) {
      for (mlir::OpOperand &use : value.getUses()) {
        auto *useOp = use.getOwner();
        if (auto storeOp = mlir::dyn_cast<mlir::LLVM::StoreOp>(useOp)) {
          if (storeOp.getValue() == value) {
            mlir::Value ptrAddr = storeOp.getAddr();
            for (mlir::OpOperand &ptrUse : ptrAddr.getUses()) {
              auto *ptrUseOp = ptrUse.getOwner();
              if (auto loadOp = mlir::dyn_cast<mlir::LLVM::LoadOp>(ptrUseOp)) {
                mlir::Value loadedVal = loadOp.getResult();
                for (mlir::OpOperand &loadUse : loadedVal.getUses()) {
                  auto *loadUseOp = loadUse.getOwner();
                  if (isConsumingOp(loadUseOp) &&
                      loadUseOp->getBlock() == point->getBlock() &&
                      loadUseOp->isBeforeInBlock(point))
                    return true;
                }
              }
            }
          }
        }
      }
    }
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
  // Tag with type name for destructor lookup during lowering.
  // Try to find the type name from the value's defining op.
  if (auto *defOp = value.getDefiningOp()) {
    if (auto allocaOp = mlir::dyn_cast<mlir::LLVM::AllocaOp>(defOp)) {
      if (auto elemType = allocaOp.getElemType()) {
        if (auto structTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(elemType)) {
          if (structTy.isIdentified()) {
            state.addAttribute("type_name",
                mlir::StringAttr::get(builder.getContext(), structTy.getName()));
          }
        }
      }
    }
  }

  // If the value is conditionally moved and has a drop flag, add the flag
  // as a second operand so OwnershipLowering can emit a conditional drop.
  auto flagIt = dropFlagMap.find(value);
  if (flagIt != dropFlagMap.end()) {
    state.addOperands(flagIt->second);
    state.addAttribute("drop_flag",
        mlir::UnitAttr::get(builder.getContext()));
  }

  builder.create(state);
}

void DropInsertionPass::insertDropFlags(mlir::func::FuncOp func) {
  mlir::Block &entryBlock = func.getBody().front();
  auto *ctx = func.getContext();
  auto i1Type = mlir::IntegerType::get(ctx, 1);

  // Find all owned values that are conditionally moved.
  // For each, insert:
  //   1. own.drop_flag_alloc at function entry (allocates i1 flag = true)
  //   2. own.drop_flag_set(flag, false) after each consuming use
  for (auto &[block, values] : ownedValuesByBlock) {
    for (auto &info : values) {
      if (!isConditionallyMoved(info.value))
        continue;

      // Insert drop_flag_alloc at the start of the entry block,
      // after any existing alloca ops and constants.
      mlir::OpBuilder entryBuilder(&entryBlock, entryBlock.begin());
      for (auto &op : entryBlock.getOperations()) {
        llvm::StringRef opName = op.getName().getStringRef();
        if (mlir::isa<mlir::LLVM::AllocaOp>(op) ||
            mlir::isa<mlir::LLVM::ConstantOp>(op) ||
            opName == "arith.constant" ||
            opName == "own.alloc" ||
            opName == "own.drop_flag_alloc" ||
            opName == "llvm.mlir.constant") {
          entryBuilder.setInsertionPointAfter(&op);
        } else if (opName == "llvm.getelementptr" || opName == "llvm.store") {
          // Also skip past struct initialization (getelementptr + store).
          entryBuilder.setInsertionPointAfter(&op);
        } else {
          break;
        }
      }

      mlir::Location loc = info.value.getLoc();
      mlir::OperationState allocState(loc, "own.drop_flag_alloc");
      allocState.addTypes(i1Type);
      mlir::Operation *flagAlloc = entryBuilder.create(allocState);
      mlir::Value flagVal = flagAlloc->getResult(0);

      // For struct allocas, follow store/load chains to find consuming uses.
      if (auto *defOp = info.value.getDefiningOp()) {
        if (isStructAlloca(defOp)) {
          for (mlir::OpOperand &use : info.value.getUses()) {
            auto *useOp = use.getOwner();
            if (auto storeOp = mlir::dyn_cast<mlir::LLVM::StoreOp>(useOp)) {
              if (storeOp.getValue() == info.value) {
                mlir::Value ptrAddr = storeOp.getAddr();
                for (mlir::OpOperand &ptrUse : ptrAddr.getUses()) {
                  auto *ptrUseOp = ptrUse.getOwner();
                  if (auto loadOp =
                          mlir::dyn_cast<mlir::LLVM::LoadOp>(ptrUseOp)) {
                    mlir::Value loadedVal = loadOp.getResult();
                    for (mlir::OpOperand &loadUse : loadedVal.getUses()) {
                      auto *loadUseOp = loadUse.getOwner();
                      if (isConsumingOp(loadUseOp) &&
                          loadUseOp->getName().getStringRef() != "own.drop") {
                        mlir::OpBuilder setBuilder(ctx);
                        setBuilder.setInsertionPointAfter(loadUseOp);
                        auto falseCst =
                            setBuilder.create<mlir::LLVM::ConstantOp>(
                                loadUseOp->getLoc(), i1Type, (int64_t)0);
                        mlir::OperationState setState(loadUseOp->getLoc(),
                                                       "own.drop_flag_set");
                        setState.addOperands({flagVal, falseCst.getResult()});
                        setBuilder.create(setState);
                      }
                    }
                  }
                }
              }
            }
          }
          dropFlagMap[info.value] = flagVal;
          continue;
        }
      }

      // For own.val typed values, insert flag set after direct consuming uses.
      for (mlir::OpOperand &use : info.value.getUses()) {
        mlir::Operation *useOp = use.getOwner();
        if (!isConsumingOp(useOp))
          continue;
        if (useOp->getName().getStringRef() == "own.drop")
          continue;

        mlir::OpBuilder setBuilder(ctx);
        setBuilder.setInsertionPointAfter(useOp);
        auto falseCst = setBuilder.create<mlir::LLVM::ConstantOp>(
            useOp->getLoc(), i1Type, (int64_t)0);
        mlir::OperationState setState(useOp->getLoc(), "own.drop_flag_set");
        setState.addOperands({flagVal, falseCst.getResult()});
        setBuilder.create(setState);
      }

      dropFlagMap[info.value] = flagVal;
    }
  }
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
    // Skip conditionally-moved struct allocas at non-return block exits.
    // These need to survive across blocks; their drops are handled at
    // return points with drop flag checks.
    if (dropFlagMap.count(info.value))
      continue;
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
        // Also include values with drop flags — they may be live across
        // blocks and need conditional dropping at return.
        if (blk == block || info.defOp == nullptr ||
            dropFlagMap.count(info.value)) {
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

  dropFlagMap.clear();

  // Step 1: Collect all owned values, indexed by block.
  collectOwnedValues(func);

  // Step 2: Insert drop flags for conditionally moved values.
  // Must run before drop insertion so insertDropBefore can attach flags.
  insertDropFlags(func);

  // Step 3: Insert drops at return statements (non-returned owned values).
  insertReturnDrops(func);

  // Step 4: Insert drops at early exits (break, continue).
  insertEarlyExitDrops(func);

  // Step 5: Insert drops at block exits for values going out of scope.
  for (auto &block : func.getBody()) {
    insertBlockExitDrops(block);
  }
}

std::unique_ptr<mlir::Pass> createDropInsertionPass() {
  return std::make_unique<DropInsertionPass>();
}

} // namespace asc
