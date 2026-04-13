// EscapeAnalysis — classifies own.alloc ops as stack-safe or must-heap.
//
// Walks all own.alloc ops in the module and traces SSA uses via BFS.
// An allocation escapes (MustHeap) if its value reaches:
//   - func.return — returned to caller, outlives the stack frame
//   - task.spawn  — captured by another thread
//   - chan.send   — sent through a channel
//   - own.store   — stored into another owned value (may outlive scope)
//
// Non-escaping allocations are marked StackSafe. The escape_status
// attribute is set on the own.alloc op so OwnershipLowering can consult it.

#include "asc/Analysis/EscapeAnalysis.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Operation.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {

//===----------------------------------------------------------------------===//
// BFS use-chain traversal to detect escaping values.
//===----------------------------------------------------------------------===//

bool EscapeAnalysisPass::escapesThroughUses(mlir::Value val) {
  llvm::SmallPtrSet<mlir::Value, 16> visited;
  llvm::SmallVector<mlir::Value, 16> worklist;
  worklist.push_back(val);
  visited.insert(val);

  while (!worklist.empty()) {
    mlir::Value current = worklist.pop_back_val();

    for (mlir::OpOperand &use : current.getUses()) {
      mlir::Operation *user = use.getOwner();
      llvm::StringRef opName = user->getName().getStringRef();

      // Direct escape: value flows to func.return.
      if (opName == "func.return")
        return true;

      // Direct escape: value captured by task.spawn.
      if (opName == "task.spawn")
        return true;

      // Direct escape: value sent through channel.
      if (opName == "chan.send")
        return true;

      // Direct escape: value stored into another owned location.
      if (opName == "own.store")
        return true;

      // Transitive: own.move produces a new SSA value — follow it.
      if (opName == "own.move") {
        for (mlir::Value res : user->getResults()) {
          if (!visited.count(res)) {
            visited.insert(res);
            worklist.push_back(res);
          }
        }
        continue;
      }

      // Transitive: own.copy — the copy itself is a new allocation,
      // but the original does not escape through copy.
      if (opName == "own.copy")
        continue;

      // Transitive: own.borrow_ref / own.borrow_mut — borrows do not
      // cause the owned value to escape (borrows cannot outlive owner).
      if (opName == "own.borrow_ref" || opName == "own.borrow_mut")
        continue;

      // own.drop — consuming the value, not escaping.
      if (opName == "own.drop")
        continue;

      // func.call — passing to a function. Conservative: if the value
      // is passed as an argument, it could escape through the callee.
      // However, since own<T> has move semantics, passing to a call
      // transfers ownership. The callee's own escape analysis would
      // handle its internal use. For now, treat calls as non-escaping
      // since the callee gets its own stack frame.
      if (opName == "func.call")
        continue;

      // LLVM dialect ops that just read the value (load, store to ptr).
      if (opName == "llvm.load" || opName == "llvm.store" ||
          opName == "llvm.getelementptr")
        continue;

      // For any other op that produces results, follow them transitively.
      // This handles unknown ops conservatively by tracking their outputs.
      for (mlir::Value res : user->getResults()) {
        if (!visited.count(res)) {
          visited.insert(res);
          worklist.push_back(res);
        }
      }
    }
  }
  return false;
}

//===----------------------------------------------------------------------===//
// Main pass logic.
//===----------------------------------------------------------------------===//

void EscapeAnalysisPass::runOnOperation() {
  auto module = getOperation();
  result = EscapeAnalysisResult(); // Reset for this run.

  // Collect all own.alloc ops.
  llvm::SmallVector<mlir::Operation *, 32> allocOps;
  module.walk([&](mlir::Operation *op) {
    if (op->getName().getStringRef() == "own.alloc")
      allocOps.push_back(op);
  });

  for (auto *op : allocOps) {
    if (op->getNumResults() == 0)
      continue;

    mlir::Value allocResult = op->getResult(0);
    bool escapes = escapesThroughUses(allocResult);

    if (escapes) {
      result.setStatus(op, EscapeStatus::MustHeap);
      op->setAttr("escape_status",
                   mlir::StringAttr::get(op->getContext(), "must_heap"));
    } else {
      result.setStatus(op, EscapeStatus::StackSafe);
      op->setAttr("escape_status",
                   mlir::StringAttr::get(op->getContext(), "stack_safe"));

      // Warn if the user explicitly annotated @heap but escape analysis
      // determines the value is stack-safe.
      if (op->hasAttr("heap")) {
        op->emitWarning()
            << "unnecessary @heap annotation -- "
            << "escape analysis determined this value is stack-safe";
      }
    }
  }
}

std::unique_ptr<mlir::Pass> createEscapeAnalysisPass() {
  return std::make_unique<EscapeAnalysisPass>();
}

} // namespace asc
