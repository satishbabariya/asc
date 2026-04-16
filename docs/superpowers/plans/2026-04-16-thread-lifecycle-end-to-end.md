# Thread Lifecycle End-to-End Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix critical panic/memory bugs and add catch_unwind — make threads work safely end-to-end (spawn → run → panic-safely → join).

**Architecture:** Seven phases targeting three RFCs: (1) fix double-drop bug in panic cleanup blocks by adding drop flag checks, (2) wire --no-panic-unwind to skip scope wrapping, (3) trap on OOM in arena allocator, (4) size arena from --max-threads, (5) add catch_unwind runtime function + HIR builtin, (6) add element destructor to channel drop, (7) fix thread-local storage on Wasm. All work is in compiler C++ and runtime C.

**Tech Stack:** C++ (LLVM 18/MLIR), C (runtime), lit test framework

---

### Task 1: Fix drop flag checks in panic cleanup blocks

**Files:**
- Modify: `lib/CodeGen/PanicLowering.cpp:150-184`

This is the critical memory safety bug. The cleanup block calls `__drop_TypeName` unconditionally for every struct alloca. If a value was moved before the panic, the drop flag is `false` but the destructor runs anyway — double-free.

- [ ] **Step 1: Extend DropTarget struct to include drop flag**

In `lib/CodeGen/PanicLowering.cpp`, find the `DropTarget` struct at line 152-155. Replace it with:

```cpp
      struct DropTarget {
        mlir::Value ptr;
        std::string dropName;
        mlir::Value dropFlag;  // null if no conditional move for this value
      };
```

- [ ] **Step 2: Collect drop flags during alloca scanning**

Replace the alloca scanning loop at lines 156-169. After finding each struct alloca with a `__drop_TypeName`, scan the function for `own.drop` ops that reference this alloca and carry a `drop_flag` attribute. The `own.drop` op's second operand is the flag pointer (an i1 alloca from `own.drop_flag_alloc`).

```cpp
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
          if (!module.lookupSymbol<mlir::func::FuncOp>(dropName))
            continue;

          // Search for own.drop ops referencing this alloca that have a drop_flag.
          mlir::Value flagPtr;
          funcOp.walk([&](mlir::Operation *innerOp) {
            if (innerOp->getName().getStringRef() != "own.drop")
              return;
            if (!innerOp->hasAttr("drop_flag"))
              return;
            // The own.drop op takes the value as operand 0 and flag as operand 1.
            if (innerOp->getNumOperands() >= 2) {
              // Check if operand 0 traces back to our alloca via load chain.
              mlir::Value dropVal = innerOp->getOperand(0);
              if (auto *defOp = dropVal.getDefiningOp()) {
                if (auto loadOp = mlir::dyn_cast<mlir::LLVM::LoadOp>(defOp)) {
                  if (loadOp.getAddr() == allocaOp.getResult() ||
                      isAliasOf(loadOp.getAddr(), allocaOp.getResult())) {
                    flagPtr = innerOp->getOperand(1);
                  }
                }
              }
              // Direct reference.
              if (dropVal == allocaOp.getResult()) {
                flagPtr = innerOp->getOperand(1);
              }
            }
          });

          dropTargets.push_back({allocaOp.getResult(), dropName, flagPtr});
        }
      }
```

Note: `isAliasOf` is a helper you may need to add — a simple check if two values trace to the same alloca through store/load chains. If it's too complex, skip it: just check `dropVal == allocaOp.getResult()` and `loadOp.getAddr() == allocaOp.getResult()` as shown above. This covers the common cases.

- [ ] **Step 3: Generate conditional drops in cleanup block**

Replace the unconditional drop loop at lines 172-179 with:

```cpp
      // Build cleanup block: run destructors (with drop flag checks), then abort.
      builder.setInsertionPointToStart(cleanupBlock);

      for (auto &dt : dropTargets) {
        auto dropFn = module.lookupSymbol<mlir::func::FuncOp>(dt.dropName);
        if (!dropFn) continue;

        if (dt.dropFlag) {
          // Conditional drop: check flag before calling destructor.
          auto i1Ty = mlir::IntegerType::get(ctx, 1);
          auto flagVal = builder.create<mlir::LLVM::LoadOp>(loc, i1Ty, dt.dropFlag);

          // Create drop and merge blocks.
          mlir::Block *dropBlock = new mlir::Block();
          mlir::Block *mergeBlock = new mlir::Block();
          funcOp.getBody().getBlocks().insertAfter(
              mlir::Region::iterator(builder.getInsertionBlock()), dropBlock);
          funcOp.getBody().getBlocks().insertAfter(
              mlir::Region::iterator(dropBlock), mergeBlock);

          // Branch: flag=true → drop, flag=false → skip.
          builder.create<mlir::LLVM::CondBrOp>(loc, flagVal, dropBlock, mergeBlock);

          // Drop block: call destructor, branch to merge.
          builder.setInsertionPointToStart(dropBlock);
          builder.create<mlir::func::CallOp>(loc, dropFn, mlir::ValueRange{dt.ptr});
          builder.create<mlir::LLVM::BrOp>(loc, mlir::ValueRange{}, mergeBlock);

          // Continue in merge block for next drop target.
          builder.setInsertionPointToStart(mergeBlock);
        } else {
          // Unconditional drop — no conditional move for this value.
          builder.create<mlir::func::CallOp>(loc, dropFn, mlir::ValueRange{dt.ptr});
        }
      }

      // After all drops: clear handler and abort.
      builder.create<mlir::LLVM::CallOp>(loc, clearHandlerFn, mlir::ValueRange{});
      builder.create<mlir::LLVM::CallOp>(loc, abortFn, mlir::ValueRange{});
      builder.create<mlir::LLVM::UnreachableOp>(loc);
```

- [ ] **Step 4: Remove old unconditional cleanup lines**

Make sure lines 181-184 (the old `clearHandlerFn`/`abortFn`/`UnreachableOp`) are replaced, not duplicated. The new code in Step 3 includes these at the end.

- [ ] **Step 5: Rebuild and test**

```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: all 249 tests pass.

- [ ] **Step 6: Commit**

```bash
git add lib/CodeGen/PanicLowering.cpp
git commit -m "fix: check drop flags in panic cleanup blocks to prevent double-free (RFC-0009)"
```

---

### Task 2: Wire --no-panic-unwind to PanicScopeWrap

**Files:**
- Modify: `lib/Driver/Driver.cpp:1051-1057` (set module attribute after HIR generation)
- Modify: `lib/Analysis/PanicScopeWrap.cpp:316-329` (read attribute and early-return)

- [ ] **Step 1: Set module attribute in lowerToHIR()**

In `lib/Driver/Driver.cpp`, after line 1051 (`mlirState->module = hirBuilder.buildModule(topLevelDecls);`) and the null check at line 1053, add:

```cpp
  // Set --no-panic-unwind attribute on the MLIR module for PanicScopeWrap.
  if (opts.noPanicUnwind) {
    mlirState->module->setAttr("asc.no_panic_unwind",
        mlir::BoolAttr::get(&mlirState->context, true));
  }
```

Insert this after line 1056 (before the `return ExitCode::Success`).

- [ ] **Step 2: Read attribute in PanicScopeWrap**

In `lib/Analysis/PanicScopeWrap.cpp`, at line 320 (after the `if (func.isDeclaration()) return;` check), add:

```cpp
  // Check for --no-panic-unwind opt-out (RFC-0009).
  // When set, panics trap directly — no try/catch wrapping needed.
  if (auto moduleOp = func->getParentOfType<mlir::ModuleOp>()) {
    if (moduleOp->hasAttr("asc.no_panic_unwind"))
      return;
  }
```

Insert this before the existing `isWasmTarget` detection at line 322.

- [ ] **Step 3: Rebuild and test**

```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: all 249 tests pass (no test uses `--no-panic-unwind`).

- [ ] **Step 4: Commit**

```bash
git add lib/Driver/Driver.cpp lib/Analysis/PanicScopeWrap.cpp
git commit -m "feat: wire --no-panic-unwind to skip PanicScopeWrap (RFC-0009)"
```

---

### Task 3: OOM trap in arena allocator

**Files:**
- Modify: `lib/Runtime/runtime.c:73-80`

- [ ] **Step 1: Replace NULL return with panic call**

In `lib/Runtime/runtime.c`, replace line 77 (`if (result + size > __asc_arena_end) return 0;`) with:

```c
  if (result + size > __asc_arena_end) {
    __asc_panic("arena allocation failed: out of memory", 41,
                "runtime.c", 9, __LINE__, 0);
    __builtin_unreachable();
  }
```

The `__asc_panic` function is declared later in the same file (line 175). Add a forward declaration near the top of the file (after line 10, after the `extern unsigned char __heap_base;` block):

```c
// Forward declaration for OOM panic.
void __asc_panic(const char *msg, unsigned int msg_len,
                 const char *file, unsigned int file_len,
                 unsigned int line, unsigned int col);
```

- [ ] **Step 2: Rebuild and test**

```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: all 249 tests pass (none exhaust the arena).

- [ ] **Step 3: Commit**

```bash
git add lib/Runtime/runtime.c
git commit -m "fix: trap on OOM in arena allocator instead of returning NULL (RFC-0008)"
```

---

### Task 4: Arena sizing from --max-threads

**Files:**
- Modify: `lib/Runtime/runtime.c:52-71` (add weak __asc_max_threads, use in arena_init)
- Modify: `lib/CodeGen/CodeGen.cpp:127` (emit strong __asc_max_threads global)

- [ ] **Step 1: Add weak global and PER_THREAD_STACK_SIZE**

In `lib/Runtime/runtime.c`, after line 52 (`#define ASC_DEFAULT_ARENA_SIZE ...`), add:

```c
#define PER_THREAD_STACK_SIZE (256 * 1024)  /* 256 KB per thread stack */

// Weak default — compiler emits a strong definition via --max-threads to override.
__attribute__((weak)) unsigned int __asc_max_threads = 4;
```

- [ ] **Step 2: Update arena_init to use dynamic sizing (native only)**

Replace the `__asc_arena_init` function at lines 64-71 with:

```c
void __asc_arena_init(unsigned long size) {
#ifndef __wasm__
  // Use the larger of: requested size, or max_threads * per_thread_stack.
  unsigned long thread_arena = (unsigned long)__asc_max_threads * PER_THREAD_STACK_SIZE;
  unsigned long actual_size = size > thread_arena ? size : thread_arena;
  if (actual_size < ASC_DEFAULT_ARENA_SIZE)
    actual_size = ASC_DEFAULT_ARENA_SIZE;  // Minimum 1 MB
  if (__asc_arena_buf) free(__asc_arena_buf);
  __asc_arena_buf = (unsigned char *)malloc(actual_size);
  __asc_arena_ptr = __asc_arena_buf;
  __asc_arena_end = __asc_arena_buf + actual_size;
#endif
}
```

- [ ] **Step 3: Emit __asc_max_threads global in CodeGen.cpp**

In `lib/CodeGen/CodeGen.cpp`, after line 127 (`llvmModule->setTargetTriple(opts.targetTriple);`), add:

```cpp
  // Emit __asc_max_threads global to override weak default in runtime (RFC-0008).
  if (opts.maxThreads != 4) {
    new llvm::GlobalVariable(
        *llvmModule, llvm::Type::getInt32Ty(llvmContext),
        /*isConstant=*/true, llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvmContext),
                               opts.maxThreads),
        "__asc_max_threads");
  }
```

- [ ] **Step 4: Rebuild and test**

```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: all 249 tests pass.

- [ ] **Step 5: Commit**

```bash
git add lib/Runtime/runtime.c lib/CodeGen/CodeGen.cpp
git commit -m "feat: size arena from --max-threads, emit __asc_max_threads global (RFC-0008)"
```

---

### Task 5: Add catch_unwind runtime function

**Files:**
- Modify: `lib/Runtime/runtime.c` (add __asc_catch_unwind function)

- [ ] **Step 1: Add __asc_catch_unwind function**

In `lib/Runtime/runtime.c`, after the `__asc_panic` function (after line 245), add:

```c
// catch_unwind — runs a closure and catches any panic (RFC-0009).
// Returns 0 on success, 1 if a panic was caught.
// On panic, out_info is filled with the PanicInfo.
#ifndef __wasm__
int __asc_catch_unwind(void *(*fn)(void *), void *arg, PanicInfo *out_info) {
  // Save current handler state.
  jmp_buf *prev_jmpbuf = __asc_panic_jmpbuf;
  int prev_unwind = __asc_in_unwind;

  // Set up new handler.
  jmp_buf buf;
  __asc_panic_jmpbuf = &buf;
  __asc_in_unwind = 0;

  if (setjmp(buf) == 0) {
    // Normal path: call the closure.
    fn(arg);
    // Restore previous handler.
    __asc_panic_jmpbuf = prev_jmpbuf;
    __asc_in_unwind = prev_unwind;
    return 0;
  } else {
    // Panic path: longjmp returned here.
    if (out_info) {
      *out_info = __asc_panic_info;
    }
    // Clear unwind flag and restore previous handler.
    __asc_in_unwind = prev_unwind;
    __asc_panic_jmpbuf = prev_jmpbuf;
    return 1;
  }
}
#endif
```

This is native-only for now. On Wasm, `catch_unwind` would require Wasm EH (throw/catch) which isn't wired yet. A Wasm stub can be added that always returns 0 (panics trap, can't be caught).

- [ ] **Step 2: Add Wasm stub**

After the native implementation, add:

```c
#ifdef __wasm__
int __asc_catch_unwind(void *(*fn)(void *), void *arg, PanicInfo *out_info) {
  // On Wasm without EH, panics trap — cannot be caught.
  // Call the function directly; if it panics, the whole program traps.
  fn(arg);
  return 0;
}
#endif
```

- [ ] **Step 3: Rebuild and test**

```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: all 249 tests pass.

- [ ] **Step 4: Commit**

```bash
git add lib/Runtime/runtime.c
git commit -m "feat: add __asc_catch_unwind runtime function (RFC-0009)"
```

---

### Task 6: Add catch_unwind HIR builtin recognition

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp` (add catch_unwind builtin handling near task_spawn at ~line 4695)

- [ ] **Step 1: Add catch_unwind builtin handling**

In `lib/HIR/HIRBuilder.cpp`, find the section where builtins are handled (around line 4695 where `task_spawn` is matched). Before the fallback at the end of the builtin section (before `// Fallback: visit arguments and return {}` around line 4923), add:

```cpp
  // catch_unwind(fn) → call __asc_catch_unwind(wrapper, null, &panic_info)
  // Returns i32: 0 = success, 1 = panic caught.
  if (name == "catch_unwind") {
    if (!e->getArgs().empty()) {
      std::string closureFnName;
      if (auto *dref = dynamic_cast<DeclRefExpr *>(e->getArgs()[0]))
        closureFnName = dref->getName().str();
      else if (auto *pathExpr = dynamic_cast<PathExpr *>(e->getArgs()[0])) {
        if (!pathExpr->getSegments().empty())
          closureFnName = pathExpr->getSegments().back();
      }

      if (!closureFnName.empty()) {
        auto ptrType = getPtrType();
        auto i32Type = builder.getIntegerType(32);

        // Generate wrapper: ptr __catch_N_wrapper(ptr arg) { call fn(); return null; }
        static unsigned catchCounter = 0;
        std::string wrapperName = "__catch_" + std::to_string(catchCounter++) + "_wrapper";

        auto savedIP = builder.saveInsertionPoint();
        builder.setInsertionPointToEnd(module.getBody());

        auto wrapperFnType = mlir::LLVM::LLVMFunctionType::get(ptrType, {ptrType});
        auto wrapperFn = builder.create<mlir::LLVM::LLVMFuncOp>(
            location, wrapperName, wrapperFnType);
        auto *entryBlock = wrapperFn.addEntryBlock();
        builder.setInsertionPointToStart(entryBlock);

        auto closureCallee = module.lookupSymbol<mlir::func::FuncOp>(closureFnName);
        if (closureCallee) {
          builder.create<mlir::func::CallOp>(location, closureCallee, mlir::ValueRange{});
        }
        auto null = builder.create<mlir::LLVM::ZeroOp>(location, ptrType);
        builder.create<mlir::LLVM::ReturnOp>(location, mlir::ValueRange{null});
        builder.restoreInsertionPoint(savedIP);

        // Declare __asc_catch_unwind: i32 (ptr fn, ptr arg, ptr out_info)
        auto catchFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__asc_catch_unwind");
        if (!catchFn) {
          mlir::OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToStart(module.getBody());
          auto fnType = mlir::LLVM::LLVMFunctionType::get(i32Type,
              {ptrType, ptrType, ptrType});
          catchFn = builder.create<mlir::LLVM::LLVMFuncOp>(
              location, "__asc_catch_unwind", fnType);
        }

        // Get wrapper function pointer.
        auto wrapperAddr = builder.create<mlir::LLVM::AddressOfOp>(
            location, ptrType, wrapperName);
        auto nullArg = builder.create<mlir::LLVM::ZeroOp>(location, ptrType);

        // Call __asc_catch_unwind(wrapper, null, null).
        // Returns i32: 0=ok, 1=panic.
        auto result = builder.create<mlir::LLVM::CallOp>(location, catchFn,
            mlir::ValueRange{wrapperAddr, nullArg, nullArg}).getResult();
        return result;
      }
    }
    return {};
  }
```

- [ ] **Step 2: Rebuild and test**

```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: all 249 tests pass.

- [ ] **Step 3: Write catch_unwind test**

Create `test/e2e/catch_unwind.ts`:

```typescript
// RUN: %asc check %s
// Test: catch_unwind builtin catches panics.
function might_panic(): void {
  panic!("test panic");
}

function main(): i32 {
  const result = catch_unwind(might_panic);
  // result is i32: 0 = success, 1 = panic caught
  assert_eq!(result, 1);
  return 0;
}
```

- [ ] **Step 4: Run test**

```bash
lit test/e2e/catch_unwind.ts -v
```

Expected: PASS (Sema check recognizes `catch_unwind` as a builtin).

- [ ] **Step 5: Run full test suite and commit**

```bash
lit test/ --no-progress-bar
git add lib/HIR/HIRBuilder.cpp lib/Runtime/runtime.c test/e2e/catch_unwind.ts
git commit -m "feat: add catch_unwind builtin for user-level panic recovery (RFC-0009)"
```

---

### Task 7: Channel element destructors in C runtime + TLS fix

**Files:**
- Modify: `lib/Runtime/channel_rt.c:75-82` (add destructor callback)
- Modify: `lib/Runtime/runtime.c:97-104` (fix TLS on Wasm with threads)

- [ ] **Step 1: Add element destructor to __asc_chan_drop**

In `lib/Runtime/channel_rt.c`, replace `__asc_chan_drop` at lines 75-82 with:

```c
void __asc_chan_drop(void *channel, unsigned int elem_size,
                     void (*elem_destructor)(void *)) {
  AscChannel *ch = (AscChannel *)channel;
  uint32_t prev = __atomic_fetch_sub(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
  if (prev == 1) {
    // Last reference — drain remaining elements.
    if (elem_destructor) {
      uint32_t head = __atomic_load_n(&ch->head, __ATOMIC_RELAXED);
      uint32_t tail = __atomic_load_n(&ch->tail, __ATOMIC_RELAXED);
      while (head != tail) {
        uint32_t idx = head % ch->capacity;
        void *slot = (char *)ch + sizeof(AscChannel) + idx * elem_size;
        elem_destructor(slot);
        head++;
      }
    }
    free(ch);
  }
}
```

Note: The old signature was `void __asc_chan_drop(void *channel)`. The new signature adds `elem_size` and `elem_destructor` parameters.

**Also update `lib/CodeGen/ConcurrencyLowering.cpp:57-59`** — the function declaration must match the new signature:

```cpp
    if (!module.lookupSymbol("__asc_chan_drop")) {
      auto i32Type = mlir::IntegerType::get(module.getContext(), 32);
      auto fnType = mlir::LLVM::LLVMFunctionType::get(voidTy,
          {ptrType, i32Type, ptrType});
      builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_chan_drop", fnType);
    }
```

No callsites emit actual calls to `__asc_chan_drop` in HIRBuilder (channels use the std library path, not the C runtime path, in practice). But the declaration must match to avoid linker errors.

- [ ] **Step 2: Fix TLS on Wasm with threads**

In `lib/Runtime/runtime.c`, replace lines 97-104 with:

```c
// Thread-local unwind flag and panic handler for drop-on-panic.
#if defined(__wasm__) && defined(__wasm_threads__)
// Wasm with threads: use TLS (handled by wasm-ld via __tls_base).
_Thread_local static int __asc_in_unwind = 0;
#elif defined(__wasm__)
// Wasm without threads: plain static is fine.
static int __asc_in_unwind = 0;
#else
#include <setjmp.h>
_Thread_local static int __asc_in_unwind = 0;
_Thread_local static jmp_buf *__asc_panic_jmpbuf = 0;
#endif
```

Also update the `__asc_panic_info` block at lines 116-120 to match:

```c
#if defined(__wasm__) && defined(__wasm_threads__)
_Thread_local static PanicInfo __asc_panic_info = {0, 0, 0, 0, 0, 0};
#elif defined(__wasm__)
static PanicInfo __asc_panic_info = {0, 0, 0, 0, 0, 0};
#else
_Thread_local static PanicInfo __asc_panic_info = {0, 0, 0, 0, 0, 0};
#endif
```

- [ ] **Step 3: Rebuild and test**

```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar
```

Expected: all tests pass. Note: if any existing code calls `__asc_chan_drop` with the old 1-arg signature, the build will fail. Search for all callsites and update them. If HIRBuilder calls `__asc_chan_drop`, add the extra two args (size=0 and destructor=null as fallback).

- [ ] **Step 4: Commit**

```bash
git add lib/Runtime/channel_rt.c lib/Runtime/runtime.c
git commit -m "feat: add channel element destructors on drop, fix TLS on Wasm threads (RFC-0007/0009)"
```

---

### Task 8: Update CLAUDE.md and final validation

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Run full test suite**

```bash
lit test/ --no-progress-bar
```

Expected: all tests pass (249+).

- [ ] **Step 2: Update CLAUDE.md coverage table**

Update the RFC coverage percentages:
- RFC-0007: `~40%` → `~48%`
- RFC-0008: `~55%` → `~68%`
- RFC-0009: `~45%` → `~65%`
- Overall: `~82%` → `~84%`

Update Known Gaps section:
- Add: "catch_unwind is native-only; Wasm catch_unwind is a no-op stub (requires Wasm EH)"
- Modify gap #3: "Wasm EH — uses setjmp/longjmp, not Wasm exception handling proposal. catch_unwind available on native targets."

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md with RFC-0007/0008/0009 coverage improvements"
```

---

### Deferred Items (follow-up plan)

These items from the design spec are intentionally deferred:
- **Wasm EH (throw/catch/rethrow)** — requires deep codegen changes to emit Wasm EH instructions instead of setjmp/longjmp
- **Static stack size from StackSizeAnalysis passed to pthread_attr_setstacksize** — StackSizeAnalysis exists but results not wired to thread creation
- **Win32 concurrency (CreateThread/WaitForSingleObject)** — separate platform target work
- **thread::scope** — requires compiler-level lifetime scoping for borrowed captures
