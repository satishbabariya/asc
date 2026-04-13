// PanicLowering — converts own.try_scope/catch_scope/cleanup_scope to
// setjmp/longjmp control flow. Runs BEFORE OwnershipLowering so the
// own.drop ops in cleanup blocks are later converted by OwnershipLowering.
//
// Architecture: two-phase collect-then-transform to avoid mutating
// the IR during iteration (which caused crashes in OwnershipLowering).

#include "asc/CodeGen/PanicLowering.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/SmallVector.h"

namespace asc {
namespace {

static mlir::LLVM::LLVMFuncOp
getOrDeclare(mlir::ModuleOp module, mlir::OpBuilder &builder,
             llvm::StringRef name, mlir::Type retTy,
             llvm::ArrayRef<mlir::Type> argTys) {
  if (auto fn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(name))
    return fn;
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  auto ty = mlir::LLVM::LLVMFunctionType::get(retTy, argTys);
  return builder.create<mlir::LLVM::LLVMFuncOp>(module.getLoc(), name, ty);
}

struct PanicLoweringPass
    : public mlir::PassWrapper<PanicLoweringPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PanicLoweringPass)

  llvm::StringRef getArgument() const override { return "asc-panic-lowering"; }
  llvm::StringRef getDescription() const override {
    return "Lower own.try_scope/catch_scope to setjmp/longjmp";
  }

  void runOnOperation() override {
    auto module = getOperation();
    auto *ctx = module.getContext();
    mlir::OpBuilder builder(ctx);

    auto ptrType = mlir::LLVM::LLVMPointerType::get(ctx);
    auto i32Type = mlir::IntegerType::get(ctx, 32);
    auto i64Type = mlir::IntegerType::get(ctx, 64);
    auto voidTy = mlir::LLVM::LLVMVoidType::get(ctx);

    // Declare runtime functions.
    auto setjmpFn = getOrDeclare(module, builder, "setjmp", i32Type, {ptrType});
    setjmpFn->setAttr("passthrough",
        builder.getArrayAttr({builder.getStringAttr("returns_twice")}));
    auto setHandlerFn = getOrDeclare(module, builder,
        "__asc_set_panic_handler", voidTy, {ptrType});
    auto clearHandlerFn = getOrDeclare(module, builder,
        "__asc_clear_panic_handler", voidTy, {});
    auto getPanicInfoFn = getOrDeclare(module, builder,
        "__asc_get_panic_info", ptrType, {});
    (void)getPanicInfoFn; // Declared to ensure symbol appears in LLVM IR.
    auto abortFn = getOrDeclare(module, builder, "abort", voidTy, {});

    // Phase 1: Collect all panic scope ops per function (no mutation).
    struct FuncScopeInfo {
      mlir::func::FuncOp func;
      mlir::Operation *tryOp = nullptr;
      mlir::Operation *cleanupOp = nullptr;
    };
    llvm::SmallVector<FuncScopeInfo, 4> funcScopes;

    // Scan module-level ops. PanicScopeWrap places try_scope/catch_scope
    // before the function they wrap. Collect pending scope ops and
    // associate with the next function encountered.
    mlir::Operation *pendingTry = nullptr;
    mlir::Operation *pendingCleanup = nullptr;
    for (auto &op : module.getBody()->getOperations()) {
      llvm::StringRef name = op.getName().getStringRef();
      if (name == "own.try_scope") {
        pendingTry = &op;
      } else if (name == "own.catch_scope" || name == "own.cleanup_scope") {
        pendingCleanup = &op;
      } else if (auto funcOp = mlir::dyn_cast<mlir::func::FuncOp>(&op)) {
        if (!funcOp.isDeclaration() && (pendingTry || pendingCleanup)) {
          FuncScopeInfo info;
          info.func = funcOp;
          info.tryOp = pendingTry;
          info.cleanupOp = pendingCleanup;
          funcScopes.push_back(info);
          pendingTry = nullptr;
          pendingCleanup = nullptr;
        }
      }
    }

    if (funcScopes.empty()) return;

    // Phase 2: Transform each function scope.
    for (auto &scope : funcScopes) {
      auto funcOp = scope.func;
      auto loc = funcOp.getLoc();
      mlir::Block &entryBlock = funcOp.getBody().front();

      // Insert setjmp setup after all allocas in the entry block.
      // This ensures alloca values dominate both the normal and cleanup paths.
      mlir::Operation *lastAlloca = nullptr;
      for (auto &op : entryBlock.getOperations()) {
        if (mlir::isa<mlir::LLVM::AllocaOp>(op))
          lastAlloca = &op;
      }
      if (lastAlloca)
        builder.setInsertionPointAfter(lastAlloca);
      else
        builder.setInsertionPointToStart(&entryBlock);

      // 1. Alloca jmp_buf (256 bytes).
      auto i8Ty = mlir::IntegerType::get(ctx, 8);
      auto jmpBufTy = mlir::LLVM::LLVMArrayType::get(i8Ty, 256);
      auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
      auto jmpBuf = builder.create<mlir::LLVM::AllocaOp>(
          loc, ptrType, jmpBufTy, one);

      // 2. Register handler.
      builder.create<mlir::LLVM::CallOp>(loc, setHandlerFn,
          mlir::ValueRange{jmpBuf});

      // 3. rc = setjmp(jmpBuf).
      auto rc = builder.create<mlir::LLVM::CallOp>(loc, setjmpFn,
          mlir::ValueRange{jmpBuf}).getResult();

      // 4. is_panic = (rc != 0).
      auto zero = builder.create<mlir::LLVM::ConstantOp>(
          loc, i32Type, (int64_t)0);
      auto isPanic = builder.create<mlir::LLVM::ICmpOp>(
          loc, mlir::LLVM::ICmpPredicate::ne, rc, zero);

      // 5. Split: everything after our setup goes to normalBlock.
      auto insertIt = builder.getInsertionPoint();
      mlir::Block *normalBlock = entryBlock.splitBlock(insertIt);

      // 6. Create cleanup block.
      mlir::Block *cleanupBlock = new mlir::Block();
      funcOp.getBody().getBlocks().insertAfter(
          mlir::Region::iterator(normalBlock), cleanupBlock);

      // 7. Conditional branch at end of setup block.
      builder.setInsertionPointToEnd(&entryBlock);
      builder.create<mlir::LLVM::CondBrOp>(
          loc, isPanic, cleanupBlock, normalBlock);

      // 8. Collect struct allocas that need drop calls on panic.
      struct DropTarget {
        mlir::Value ptr;
        std::string dropName;
      };
      llvm::SmallVector<DropTarget, 4> dropTargets;
      for (auto &block : funcOp.getBody()) {
        for (auto &op : block) {
          auto allocaOp = mlir::dyn_cast<mlir::LLVM::AllocaOp>(&op);
          if (!allocaOp) continue;
          auto elemType = allocaOp.getElemType();
          if (!elemType) continue;
          auto structTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(elemType);
          if (!structTy || !structTy.isIdentified()) continue;
          std::string dropName = "__drop_" + structTy.getName().str();
          if (module.lookupSymbol<mlir::func::FuncOp>(dropName))
            dropTargets.push_back({allocaOp.getResult(), dropName});
        }
      }

      // Build cleanup block: run destructors, then clear handler + abort.
      builder.setInsertionPointToStart(cleanupBlock);

      for (auto &dt : dropTargets) {
        auto dropFn = module.lookupSymbol<mlir::func::FuncOp>(dt.dropName);
        if (dropFn)
          builder.create<mlir::func::CallOp>(loc, dropFn,
              mlir::ValueRange{dt.ptr});
      }

      builder.create<mlir::LLVM::CallOp>(loc, clearHandlerFn,
          mlir::ValueRange{});
      builder.create<mlir::LLVM::CallOp>(loc, abortFn, mlir::ValueRange{});
      builder.create<mlir::LLVM::UnreachableOp>(loc);

      // 9. Insert clear_panic_handler before every return on normal path.
      llvm::SmallVector<mlir::func::ReturnOp, 4> returns;
      funcOp.walk([&](mlir::func::ReturnOp retOp) {
        if (retOp->getBlock() != cleanupBlock)
          returns.push_back(retOp);
      });
      for (auto retOp : returns) {
        mlir::OpBuilder retBuilder(retOp);
        retBuilder.create<mlir::LLVM::CallOp>(retOp.getLoc(), clearHandlerFn,
            mlir::ValueRange{});
      }

      // 10. Erase original try/catch/cleanup ops.
      if (scope.cleanupOp) scope.cleanupOp->erase();
      if (scope.tryOp) scope.tryOp->erase();

      // Erase any standalone own.rethrow/own.resume.
      llvm::SmallVector<mlir::Operation *, 4> toErase;
      funcOp.walk([&](mlir::Operation *op) {
        llvm::StringRef n = op->getName().getStringRef();
        if (n == "own.rethrow" || n == "own.resume")
          toErase.push_back(op);
      });
      for (auto *op : toErase) op->erase();
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> createPanicLoweringPass() {
  return std::make_unique<PanicLoweringPass>();
}

} // namespace asc
