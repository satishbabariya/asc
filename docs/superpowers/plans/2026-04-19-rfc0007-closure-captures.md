# RFC-0007 Phase 1 — Closure-Literal Captures in task.spawn

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow `task.spawn(|| { ... })` (closure literal) as the first argument to `task.spawn`, with Send-validated captures, by lifting the closure to a module-level function and routing through the existing named-function + env-struct spawn path.

**Architecture:** Three layers. (1) Sema performs free-variable analysis on the closure body, resolves each free var to a binding in the enclosing scope, and validates that every captured binding's type is Send. The current blanket rejection at `SemaExpr.cpp:976` is replaced by this validator. (2) HIRBuilder, when `task_spawn`'s first arg is a `ClosureExpr`, synthesizes a named `func.func` from the closure body (one parameter per captured free var), then reuses the existing multi-arg env-struct path by treating the captures as if they were additional positional args. (3) The wrapper function emitted by the existing path already handles env-struct unpacking, so no new pthread wiring is required.

**Tech Stack:** C++ (LLVM 18 / MLIR), lit test framework, wasmtime for e2e validation.

**Baseline:** 295/295 lit tests passing. RFC-0007 coverage 48% → expected ~58% after Phase 1.

---

### Task 1: Extract free-var collector into a reusable Sema helper

The free-variable collector `collectFreeVars` currently lives in `lib/HIR/HIRBuilder.cpp` starting near line 3700. Sema needs to call it too — duplicating it is a DRY violation. Move the logic to a new header so both Sema and HIRBuilder share it.

**Files:**
- Create: `include/asc/Analysis/FreeVars.h`
- Create: `lib/Analysis/FreeVars.cpp`
- Modify: `lib/HIR/HIRBuilder.cpp` (delete inline `collectFreeVars`, call helper)
- Modify: `lib/Analysis/CMakeLists.txt` (add FreeVars.cpp to target)

- [ ] **Step 1: Read the current inline implementation**

Read `lib/HIR/HIRBuilder.cpp` from the start of `collectFreeVars` (locate with `grep -n "^void HIRBuilder::collectFreeVars\|^static void collectFreeVars\|collectFreeVars("` ) through its closing brace. Capture the full logic: it walks every `ExprKind` and pushes identifier names into a `StringSet` if they are not in `paramNames` and not defined by an inner `let`.

- [ ] **Step 2: Write the header**

Create `include/asc/Analysis/FreeVars.h` with:

```cpp
#ifndef ASC_ANALYSIS_FREEVARS_H
#define ASC_ANALYSIS_FREEVARS_H

#include "asc/AST/Expr.h"
#include "llvm/ADT/StringSet.h"

namespace asc {

/// Collect identifiers referenced inside `expr` that are not bound by
/// `boundNames` (closure params + inner `let`s). Used by both Sema (to
/// validate Send on captures) and HIRBuilder (to synthesize env structs).
void collectFreeVars(Expr *expr,
                     const llvm::StringSet<> &boundNames,
                     llvm::StringSet<> &freeVars);

} // namespace asc

#endif
```

- [ ] **Step 3: Write the source file**

Create `lib/Analysis/FreeVars.cpp`. Copy the logic from the inline `collectFreeVars` in `HIRBuilder.cpp` verbatim, but change the namespace to `asc` and change the signature to free-function form (no `HIRBuilder::` qualifier). Preserve every `dynamic_cast` branch exactly — the walker must cover `BinaryExpr`, `UnaryExpr`, `CallExpr`, `IfExpr`, `BlockExpr`, `DeclRefExpr`, `ParenExpr`, `TaskScopeExpr`, and any other branches present.

- [ ] **Step 4: Update CMake**

Open `lib/Analysis/CMakeLists.txt`. Find the `add_library` or `target_sources` call that lists `.cpp` files (pattern: `LivenessAnalysis.cpp`, `RegionInference.cpp`, etc.). Add `FreeVars.cpp` to the list in alphabetical order.

- [ ] **Step 5: Update HIRBuilder to call the helper**

In `lib/HIR/HIRBuilder.cpp`:
1. Add `#include "asc/Analysis/FreeVars.h"` near the top with other `asc/Analysis/` includes.
2. Delete the entire `collectFreeVars` method/function body (its definition and, if present, its declaration in the header).
3. At every call site (around `HIRBuilder::visitClosureExpr` and any other users), change `collectFreeVars(e->getBody(), paramNames, freeVarNames)` to `asc::collectFreeVars(e->getBody(), paramNames, freeVarNames)`.

- [ ] **Step 6: Build**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu)
```

Expected: clean build, no new warnings.

- [ ] **Step 7: Run full test suite**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: 295/295 pass. This is a pure refactor — no behavior change.

- [ ] **Step 8: Commit**

```bash
git add include/asc/Analysis/FreeVars.h lib/Analysis/FreeVars.cpp \
        lib/Analysis/CMakeLists.txt lib/HIR/HIRBuilder.cpp
git commit -m "analysis: extract free-var collector into shared helper for Sema/HIR reuse"
```

---

### Task 2: Write a failing Sema test for closure-literal task.spawn

Before removing the current rejection, write the test that will pass once Phase 1 is complete. This is the end-state acceptance test.

**Files:**
- Create: `test/Sema/task_spawn_closure_primitive.ts`

- [ ] **Step 1: Write the test**

Create `test/Sema/task_spawn_closure_primitive.ts`:

```typescript
// RUN: %asc check %s 2>&1 | FileCheck %s --allow-empty
// CHECK-NOT: error

// Primitive captures are trivially Send — the canonical positive case.
fn main() -> i32 {
  let x: i32 = 42;
  let h = task.spawn(|| {
    let _ = x + 1;
  });
  task.join(h);
  return 0;
}
```

- [ ] **Step 2: Run the test — should FAIL**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/Sema/task_spawn_closure_primitive.ts -v
```

Expected: FAIL with the current rejection error ("task.spawn does not yet support closure literals"). This is the regression we're removing.

---

### Task 3: Add Sema capture validation (Send check, no rejection)

Replace the blanket rejection in `checkMacroCallExpr` with per-capture Send validation.

**Files:**
- Modify: `lib/Sema/SemaExpr.cpp:964-984`
- Modify: `include/asc/Sema/Sema.h` (if a helper method needs to be declared)

- [ ] **Step 1: Read the current rejection**

Re-read `lib/Sema/SemaExpr.cpp` lines 964–990 (the `checkMacroCallExpr` body). The current logic simply emits an error if arg 0 of `task_spawn` is `ExprKind::Closure`. We keep the method structure but replace the rejection with validation.

- [ ] **Step 2: Find a Send-checking helper**

Search Sema for an existing "is this type Send?" helper:

```bash
grep -n "isSendType\|is_send\|checkSend" /Users/satishbabariya/Desktop/asc/lib/Sema/*.cpp /Users/satishbabariya/Desktop/asc/include/asc/Sema/*.h
```

If one exists (likely in `SemaType.cpp` or `Sema.cpp` near trait-marker checks), use it. If not, write it as described in Step 3. If found, skip to Step 4.

- [ ] **Step 3: Add `isSendType` helper (only if not found in Step 2)**

Add to `include/asc/Sema/Sema.h` inside the `Sema` class, in the private section near other `is*Type` helpers:

```cpp
  /// True if T is Send: all primitives are Send; structs/enums are Send iff
  /// declared @send OR derived Send. Non-Send examples: Rc<T>, raw pointers
  /// to non-Send data.
  bool isSendType(Type *t) const;
```

Add the body to `lib/Sema/Sema.cpp`:

```cpp
bool Sema::isSendType(Type *t) const {
  if (!t) return false;
  if (dynamic_cast<BuiltinType *>(t)) return true;  // primitives are Send
  if (auto *nt = dynamic_cast<NamedType *>(t)) {
    auto it = structDecls.find(nt->getName());
    if (it != structDecls.end()) {
      // @send attribute or Send in derive list.
      for (const auto &attr : it->second->getAttributes())
        if (attr.name == "send") return true;
      return hasDerive(it->second, "Send");
    }
    auto eit = enumDecls.find(nt->getName());
    if (eit != enumDecls.end()) {
      for (const auto &attr : eit->second->getAttributes())
        if (attr.name == "send") return true;
      return hasDerive(eit->second, "Send");
    }
    // Prelude Send-safe types.
    llvm::StringRef n = nt->getName();
    if (n == "String" || n.starts_with("Vec") ||
        n.starts_with("HashMap") || n.starts_with("Arc") ||
        n.starts_with("Box") || n == "i8" || n == "i16" ||
        n == "i32" || n == "i64" || n == "u8" || n == "u16" ||
        n == "u32" || n == "u64" || n == "usize" || n == "isize" ||
        n == "f32" || n == "f64" || n == "bool")
      return true;
    // Rc is explicitly NOT Send.
    if (n.starts_with("Rc")) return false;
  }
  // Unknown → conservatively Send-unknown. Treat as non-Send.
  return false;
}
```

Note: `hasDerive` is assumed to exist (used by derive machinery per `CLAUDE.md` "Derive Support"). If it doesn't, replace both `hasDerive(...)` calls with a loop over `getDeriveList()` looking for a matching name. Verify:

```bash
grep -n "hasDerive\|getDeriveList" /Users/satishbabariya/Desktop/asc/include/asc/AST/*.h
```

- [ ] **Step 4: Rewrite the task_spawn guard in checkMacroCallExpr**

In `lib/Sema/SemaExpr.cpp`, replace lines 970–984 (the closure-rejection block) with:

```cpp
  // task.spawn: a closure-literal first arg is permitted; each captured
  // free variable's type must be Send (RFC-0007). A non-closure first arg
  // (named function) is validated by the existing multi-arg path.
  if (name == "task_spawn" && !e->getArgs().empty()) {
    if (auto *cl = dynamic_cast<ClosureExpr *>(e->getArgs()[0])) {
      llvm::StringSet<> boundNames;
      for (const auto &p : cl->getParams())
        boundNames.insert(p.name);
      llvm::StringSet<> freeVars;
      asc::collectFreeVars(cl->getBody(), boundNames, freeVars);
      for (const auto &entry : freeVars) {
        llvm::StringRef varName = entry.getKey();
        Symbol *sym = currentScope ? currentScope->lookup(varName) : nullptr;
        if (!sym || !sym->type) continue;  // unresolved — later passes error
        if (!isSendType(sym->type)) {
          diags.emitError(cl->getLocation(), DiagID::ErrTypeMismatch,
              ("captured variable '" + varName.str() +
               "' is not Send; task.spawn requires Send captures").c_str());
        }
      }
    }
  }
```

Add includes at the top of `lib/Sema/SemaExpr.cpp` if missing:

```cpp
#include "asc/Analysis/FreeVars.h"
#include "llvm/ADT/StringSet.h"
```

- [ ] **Step 5: Build**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu)
```

Expected: clean build.

- [ ] **Step 6: Rerun the Task 2 test**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/Sema/task_spawn_closure_primitive.ts -v
```

Expected: **still FAIL**, but now with a different error. Sema will pass (no more "closure literal not supported" error), but HIRBuilder will silently drop the closure → the generated code will be wrong or a verifier error will surface. The next task fixes HIRBuilder.

If Sema now emits no error for the primitive case, this step is working. If Sema errors remain, diagnose the capture validation before proceeding.

- [ ] **Step 7: Run full test suite**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: 295/295 still pass. No existing test uses closure-literal `task.spawn`, and the rejection path we removed was unreachable for existing tests.

- [ ] **Step 8: Commit**

```bash
git add lib/Sema/SemaExpr.cpp include/asc/Sema/Sema.h lib/Sema/Sema.cpp \
        test/Sema/task_spawn_closure_primitive.ts
git commit -m "sema: permit closure-literal task.spawn with per-capture Send validation (RFC-0007)"
```

---

### Task 4: Write a failing negative Sema test for non-Send capture

**Files:**
- Create: `test/Sema/task_spawn_closure_non_send.ts`

- [ ] **Step 1: Write the test**

```typescript
// RUN: %asc check %s 2>&1 | FileCheck %s
// CHECK: error
// CHECK: not Send

fn main() -> i32 {
  let rc = Rc::new(42);
  let h = task.spawn(|| {
    let _ = rc;
  });
  task.join(h);
  return 0;
}
```

- [ ] **Step 2: Run it**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/Sema/task_spawn_closure_non_send.ts -v
```

Expected: **PASS** (the test asserts an error is emitted; our Sema validator emits it). If it fails because no error is emitted, the `isSendType` logic is too permissive for `Rc`. Fix `isSendType` so `Rc<…>` returns false.

- [ ] **Step 3: Commit**

```bash
git add test/Sema/task_spawn_closure_non_send.ts
git commit -m "test(sema): reject task.spawn of closure capturing Rc (non-Send)"
```

---

### Task 5: Synthesize a module-level function from a closure-literal task.spawn in HIRBuilder

Extend the existing `task_spawn` handler in `HIRBuilder.cpp` (starts at line 4781). When arg 0 is a `ClosureExpr`, lift the closure body to a new `func.func` whose parameters are the captured free-vars (in sorted order for determinism), then route through the existing multi-arg env-struct path.

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp:4781-4979`

- [ ] **Step 1: Read the full existing task_spawn handler**

Reread `lib/HIR/HIRBuilder.cpp:4781-4979` to hold the structure in context. Key facts:
- After extracting `closureFnName` from `DeclRefExpr` or `PathExpr`, the code computes `numCaptured = args.size() - 1` and packs args[1..] into an env struct (single-arg and multi-arg paths).
- The wrapper unpacks via GEP to call the lifted function with each field as a scalar argument.

Our job: produce a synthetic `closureFnName` and a synthetic "captured args list" that mirrors the free-vars of the closure.

- [ ] **Step 2: Add the closure-literal branch**

Right before the existing `if (auto *dref = dynamic_cast<DeclRefExpr *>(e->getArgs()[0]))` on line 4788, insert:

```cpp
      // Closure-literal first arg (RFC-0007): synthesize a module-level
      // function whose parameters are the closure's captured free vars,
      // emit the body, then treat the captures as additional positional
      // args and fall through to the existing env-struct packer.
      if (auto *cl = dynamic_cast<ClosureExpr *>(e->getArgs()[0])) {
        // 1. Collect free vars (deterministic order: sorted by name).
        llvm::StringSet<> paramNames;
        for (const auto &p : cl->getParams())
          paramNames.insert(p.name);
        llvm::StringSet<> freeVarSet;
        asc::collectFreeVars(cl->getBody(), paramNames, freeVarSet);
        llvm::SmallVector<std::string> freeVars;
        for (auto &ent : freeVarSet) freeVars.push_back(ent.getKey().str());
        std::sort(freeVars.begin(), freeVars.end());

        // 2. Resolve each free var in the current HIR scope; record its type.
        llvm::SmallVector<mlir::Value> capturedVals;
        llvm::SmallVector<mlir::Type> capturedTypes;
        for (const auto &vn : freeVars) {
          mlir::Value v = lookup(vn);
          if (!v) continue;
          // Load if alloca-backed (match visitClosureExpr logic).
          if (mlir::isa<mlir::LLVM::LLVMPointerType>(v.getType())) {
            if (auto *defOp = v.getDefiningOp()) {
              if (auto alc = mlir::dyn_cast<mlir::LLVM::AllocaOp>(defOp)) {
                mlir::Type et = alc.getElemType();
                if (et && (et.isIntOrIndexOrFloat() ||
                           mlir::isa<mlir::LLVM::LLVMPointerType>(et))) {
                  v = builder.create<mlir::LLVM::LoadOp>(location, et, v);
                }
              }
            }
          }
          capturedVals.push_back(v);
          capturedTypes.push_back(v.getType());
        }

        // 3. Synthesize the module-level function.
        static unsigned spawnClosureCounter = 0;
        std::string liftedName = "__spawn_closure_" +
                                 std::to_string(spawnClosureCounter++);
        {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(module.getBody());
          auto voidTy = mlir::LLVM::LLVMVoidType::get(builder.getContext());
          auto fnTy = builder.getFunctionType(capturedTypes, {});
          auto liftedFn = builder.create<mlir::func::FuncOp>(
              location, liftedName, fnTy);
          auto *entry = liftedFn.addEntryBlock();
          builder.setInsertionPointToStart(entry);

          // Bind each free var to its block argument in a fresh scope.
          pushScope();
          for (size_t i = 0; i < freeVars.size() &&
                              i < entry->getNumArguments(); ++i) {
            bind(freeVars[i], entry->getArgument(i));
          }
          // Bind closure params too (for completeness; a closure used as
          // task body typically takes no params, but be safe).
          // No block-arg mapping needed: task bodies are zero-arity here.

          // Emit the body.
          if (cl->getBody()) visitExpr(cl->getBody());

          // Return void.
          builder.create<mlir::func::ReturnOp>(location);
          popScope();
          (void)voidTy;
        }

        // 4. Route through existing path: pretend closureFnName is the lifted
        // fn and the captured vals are args[1..N]. We must mimic the
        // existing logic that reads e->getArgs()[1..]. Use a local shim:
        closureFnName = liftedName;
        // We will bypass the arg-list walk below by capturing pre-evaluated
        // vals into a local vector and using numCaptured = capturedVals.size().
        // Emit the pthread_create using the same code path — but the
        // `visitExpr(e->getArgs()[i])` loop would fail because args[0] is
        // the closure, not a value. Instead, inline the env-struct packer
        // here with capturedVals already evaluated.

        auto ptrType = getPtrType();
        auto i32Type = builder.getIntegerType(32);
        auto i64Type = builder.getIntegerType(64);

        // Build the wrapper (same logic as the numInputs > 1 branch).
        static unsigned taskCounterCl = 0;
        std::string wrapperName = "__task_cl_" +
            std::to_string(taskCounterCl++) + "_wrapper";
        {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(module.getBody());
          auto wrapperFnType =
              mlir::LLVM::LLVMFunctionType::get(ptrType, {ptrType});
          auto wrapperFn = builder.create<mlir::LLVM::LLVMFuncOp>(
              location, wrapperName, wrapperFnType);
          auto *entryBlock = wrapperFn.addEntryBlock();
          builder.setInsertionPointToStart(entryBlock);

          auto liftedCallee =
              module.lookupSymbol<mlir::func::FuncOp>(liftedName);
          if (liftedCallee && !capturedTypes.empty()) {
            auto envStructTy = mlir::LLVM::LLVMStructType::getLiteral(
                builder.getContext(), capturedTypes, /*isPacked=*/true);
            mlir::Value envPtr = entryBlock->getArgument(0);
            llvm::SmallVector<mlir::Value> callArgs;
            for (unsigned i = 0; i < capturedTypes.size(); ++i) {
              auto idx = builder.create<mlir::LLVM::ConstantOp>(
                  location, i32Type, static_cast<int64_t>(i));
              auto zero = builder.create<mlir::LLVM::ConstantOp>(
                  location, i32Type, static_cast<int64_t>(0));
              auto fieldPtr = builder.create<mlir::LLVM::GEPOp>(
                  location, ptrType, envStructTy, envPtr,
                  mlir::ValueRange{zero, idx});
              auto val = builder.create<mlir::LLVM::LoadOp>(
                  location, capturedTypes[i], fieldPtr);
              callArgs.push_back(val);
            }
            builder.create<mlir::func::CallOp>(location, liftedCallee,
                mlir::ValueRange(callArgs));
          } else if (liftedCallee) {
            // Zero captures → call with no args.
            builder.create<mlir::func::CallOp>(location, liftedCallee,
                mlir::ValueRange{});
          }
          auto null =
              builder.create<mlir::LLVM::ZeroOp>(location, ptrType);
          builder.create<mlir::LLVM::ReturnOp>(location,
              mlir::ValueRange{null});
        }

        // Declare pthread_create.
        auto pthreadCreateFn =
            module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("pthread_create");
        if (!pthreadCreateFn) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToStart(module.getBody());
          auto fnType = mlir::LLVM::LLVMFunctionType::get(i32Type,
              {ptrType, ptrType, ptrType, ptrType});
          pthreadCreateFn = builder.create<mlir::LLVM::LLVMFuncOp>(
              location, "pthread_create", fnType);
        }

        // Alloca pthread_t; addressof wrapper.
        auto i64One = builder.create<mlir::LLVM::ConstantOp>(
            location, i64Type, static_cast<int64_t>(1));
        auto tidAlloca = builder.create<mlir::LLVM::AllocaOp>(
            location, ptrType, i64Type, i64One);
        auto wrapperAddr = builder.create<mlir::LLVM::AddressOfOp>(
            location, ptrType, wrapperName);

        // Malloc + pack captures into env struct.
        mlir::Value threadArg;
        if (!capturedVals.empty()) {
          auto envStructTy = mlir::LLVM::LLVMStructType::getLiteral(
              builder.getContext(), capturedTypes, /*isPacked=*/true);
          uint64_t totalSize = 0;
          for (auto t : capturedTypes) totalSize += getTypeSize(t);
          if (totalSize == 0) totalSize = 8;
          auto mallocFn =
              module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
          if (!mallocFn) {
            mlir::OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToStart(module.getBody());
            auto fnType = mlir::LLVM::LLVMFunctionType::get(ptrType,
                {i64Type});
            mallocFn = builder.create<mlir::LLVM::LLVMFuncOp>(
                location, "malloc", fnType);
          }
          auto sizeConst = builder.create<mlir::LLVM::ConstantOp>(
              location, i64Type, static_cast<int64_t>(totalSize));
          threadArg = builder.create<mlir::LLVM::CallOp>(
              location, mallocFn, mlir::ValueRange{sizeConst}).getResult();
          for (unsigned i = 0; i < capturedVals.size(); ++i) {
            auto idx = builder.create<mlir::LLVM::ConstantOp>(
                location, i32Type, static_cast<int64_t>(i));
            auto zero = builder.create<mlir::LLVM::ConstantOp>(
                location, i32Type, static_cast<int64_t>(0));
            auto fieldPtr = builder.create<mlir::LLVM::GEPOp>(
                location, ptrType, envStructTy, threadArg,
                mlir::ValueRange{zero, idx});
            builder.create<mlir::LLVM::StoreOp>(location,
                capturedVals[i], fieldPtr);
          }
        } else {
          threadArg = builder.create<mlir::LLVM::ZeroOp>(location, ptrType);
        }

        auto nullAttr =
            builder.create<mlir::LLVM::ZeroOp>(location, ptrType);
        builder.create<mlir::LLVM::CallOp>(location, pthreadCreateFn,
            mlir::ValueRange{tidAlloca, nullAttr, wrapperAddr, threadArg});

        if (!taskScopeHandleStack.empty())
          taskScopeHandleStack.back().push_back(tidAlloca);

        return tidAlloca;
      }
```

- [ ] **Step 3: Add include**

Ensure `lib/HIR/HIRBuilder.cpp` has:

```cpp
#include "asc/Analysis/FreeVars.h"
```

and `#include <algorithm>` for `std::sort`. Add them near existing includes if missing.

- [ ] **Step 4: Build**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu)
```

Expected: clean build. Watch for MLIR verifier errors — if the module fails to verify, the most likely cause is the lifted func signature not matching the wrapper's env-struct layout.

- [ ] **Step 5: Rerun the Task 2 test**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/Sema/task_spawn_closure_primitive.ts -v
```

Expected: PASS (Sema check alone, no execution).

- [ ] **Step 6: Run full test suite**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: 295/295 pass. Closure literals in non-task-spawn contexts still go through the original `visitClosureExpr` path — we did not touch it.

- [ ] **Step 7: Commit**

```bash
git add lib/HIR/HIRBuilder.cpp
git commit -m "hir: lower task.spawn(closure) by lifting body to a named fn + env struct (RFC-0007)"
```

---

### Task 6: Add an e2e test that compiles and runs

Verify the full path (Sema → HIR → CodeGen → link → execute) with a native target, which is the simplest runnable validation.

**Files:**
- Create: `test/e2e/task_spawn_closure_run.ts`

- [ ] **Step 1: Write the test**

```typescript
// RUN: %asc build %s --target aarch64-apple-darwin --emit obj -o %t.o
// RUN: cc %t.o -o %t.bin -lpthread
// RUN: %t.bin
// RUN: echo $? | FileCheck %s
// CHECK: 0

fn main() -> i32 {
  let x: i32 = 7;
  let y: i32 = 35;
  let h = task.spawn(|| {
    let sum = x + y;
    if sum != 42 {
      panic!("bad capture");
    }
  });
  task.join(h);
  return 0;
}
```

Note: the `RUN` lines depend on lit substitutions available in the repo. Inspect `test/lit.cfg*` to confirm `%asc` is the binary and platform linker is available. If the current test harness is wasm-centric, change the target and linker to match existing native tests in `test/e2e/`.

Verify by listing a similar existing native test:

```bash
ls /Users/satishbabariya/Desktop/asc/test/e2e/ | head -20
```

Pick one that already uses a native triple and mirror its RUN lines exactly.

- [ ] **Step 2: Run the test**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/task_spawn_closure_run.ts -v
```

Expected: PASS. The child thread reads `x=7` and `y=35`, verifies the sum is 42, exits normally; `task.join` waits; main returns 0.

If it fails with a link error about `pthread_create`, confirm the existing `task_spawn_two_args.ts` (or similar) test's RUN lines and copy them. If it segfaults, the env-struct packing is off — verify `getTypeSize` returns the correct size for `i32` (4 bytes) and the struct is packed (not aligned).

- [ ] **Step 3: Run full test suite**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: 296/296 pass (295 existing + 1 new).

- [ ] **Step 4: Commit**

```bash
git add test/e2e/task_spawn_closure_run.ts
git commit -m "test(e2e): task.spawn with closure literal captures runs end-to-end"
```

---

### Task 7: Update CLAUDE.md to reflect the closed gap

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update the Known Gaps list**

Open `CLAUDE.md`. Locate "Known Gaps" (search for `## Known Gaps`). Replace gap #1 (the current paragraph about closure literals being rejected) with:

```
1. **Closure literals in task.spawn** — supported. Free variables are collected,
   Send-validated by Sema, and lowered to a synthesized module-level function
   + env-struct packing shared with the named-function form. Open follow-ups:
   moves of non-Copy captures are currently copies (should be moves);
   auto-cloning `Arc`/`Rc` is still manual.
```

- [ ] **Step 2: Update the RFC coverage table**

In the RFC table, change RFC-0007 from `~48%` to `~58%` and overall weighted from `~85%` to `~86%`.

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: RFC-0007 closure-literal captures landed; coverage 48% → 58%"
```

---

### Task 8: Final validation

- [ ] **Step 1: Run the full suite one more time**

```bash
cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: all tests pass, including the 2 new Sema tests and 1 new e2e test.

- [ ] **Step 2: Check git log**

```bash
git log --oneline -10
```

Expected: 6 commits on top of the baseline, one per task that produced code/tests/docs.

- [ ] **Step 3: Write the handoff note**

If handing off to a later session or PR reviewer, summarize: "Phase 1 of RFC-0007 complete. Closure literals in task.spawn validated by Sema for Send captures, lowered by lifting the body to `__spawn_closure_N` and reusing the existing env-struct path. 3 new tests. Known follow-ups: move semantics on captures, auto-clone for Arc/Rc, thread::scope (Phase 6)."

---

### Deferred to later RFC-0007 phases

These items were explicitly scoped out of Phase 1 and have their own follow-up plans:

- **Phase 2** — Wasm `wasi_thread_start` lowering (native-only today)
- **Phase 3** — Static stack-size analysis wired to `pthread_attr_setstacksize`
- **Phase 4** — MPMC channels (currently SPSC only)
- **Phase 5** — Futex-style wait/notify for mutex/rwlock/semaphore on Wasm
- **Phase 6** — `thread::scope` with lifetime-bounded borrow captures

Each will be written as a separate plan under `docs/superpowers/plans/` when Phase 1 merges.
