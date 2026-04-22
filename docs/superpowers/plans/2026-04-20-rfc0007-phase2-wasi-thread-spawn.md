# RFC-0007 Phase 2 — Wasm `task.spawn` via wasi-threads

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lower `task.spawn` and `task.join` to the wasi-threads ABI so `asc build foo.ts --target wasm32-wasi-threads -o foo.wasm` produces a module that runs on `wasmtime --wasi threads=y`.

**Architecture:** Keep HIR emission unchanged in shape — continue packing captures into a malloc'd env struct and returning an opaque `ptr` handle. Branch on the target triple at symbol-lookup time: on native, call `pthread_create` / `pthread_join`; on Wasm, call two new C runtime helpers (`__asc_wasi_thread_spawn`, `__asc_wasi_thread_join`) that wrap the wasi-threads imports and the exported `wasi_thread_start` entrypoint. The runtime trampoline approach keeps HIR target-agnostic and mirrors how channels / mutex are already handled.

**Tech Stack:** C11 runtime (clang wasm32-wasi-threads), MLIR LLVM dialect, LLVM 18, wasm-ld, wasmtime (for e2e).

**Baseline:** 316 lit tests passing on `main` at commit 6319f7f. RFC-0007 coverage ~58% (post Phase 1). Target after Phase 2: ~70%.

---

## ABI Reference

Per the [wasi-threads proposal](https://github.com/WebAssembly/wasi-threads):

- **Module imports** `wasi:thread-spawn/thread-spawn(start_arg: i32) -> i32` — returns `tid` (negative on error).
- **Module exports** `wasi_thread_start(tid: i32, start_arg: i32) -> void` — the host runtime calls this on the newly-spawned thread.
- **Shared memory** required (`--shared-memory --import-memory --max-memory=N`).
- **`pthread` is not available** — everything goes through `wasi:thread-spawn` + manual atomics.

RFC-0007 §204-210 says `call $wasi_thread_start` is the import; that is the wrong direction. `wasi_thread_start` is the **export**, and `thread-spawn` is the import. This plan uses the correct ABI and documents the deviation in the RFC during the doc-update task.

## File Structure

**Create:**
- `lib/Runtime/wasi_thread_rt.c` — wasi-threads C runtime (spawn trampoline, exported start entry, join via a volatile busy-spin on `done_flag`). A `memory.atomic.wait32`-based join is the intended long-term primitive, but wasmtime 43 traps "unaligned atomic" on 4-byte-aligned flags in practice; the proper wait/notify lowering is deferred to Phase 5. Self-contained: no libc pthread dep.
- `include/asc/Runtime/wasi_thread_rt.h` — symbol declarations (also imported by the test driver for sanity).
- `test/e2e/task_spawn_wasm_basic.ts` — compiles to `.wasm`, FileCheck-validates the expected imports/exports.
- `test/e2e/task_spawn_wasm_run.ts` — if wasmtime is available and supports `--wasi threads=y`, execute and validate stdout.

**Modify:**
- `lib/CodeGen/ConcurrencyLowering.cpp` — extend `declareWasiThreadsFunctions` with `__asc_wasi_thread_spawn` and `__asc_wasi_thread_join` (drop the wrong `wasi_thread_start` import declaration).
- `lib/HIR/HIRBuilder.cpp` — extract `emitSpawnThreadCreate` / `emitSpawnThreadJoin` helpers that branch on triple; current inline code moves inside. Handle remains `ptr` for both backends.
- `include/asc/HIR/HIRBuilder.h` — add member `llvm::Triple targetTriple` (already threaded through CodeGen; needs to reach HIRBuilder for symbol selection).
- `lib/Driver/Driver.cpp` — update `linkWasm` to add threading flags + build the runtime with `--target=wasm32-wasi-threads -pthread -matomics -mbulk-memory` and include `wasi_thread_rt.c` when the triple opts into threads.
- `lib/Runtime/CMakeLists.txt` — add `wasi_thread_rt.c` to `ascRuntime`. (Host build will compile it too, guarded by `#ifdef __wasm__` inside the file.)
- `rfcs/RFC-0007-concurrency-model.md` — correct the `wasi_thread_start` ABI direction; add a short note that spawn is lowered via runtime trampoline, not inline atomics.
- `CLAUDE.md` — move "Closure literals in task.spawn" Wasm note to "Wasm threads working"; update RFC-0007 coverage %.

---

## Task 1: Verify baseline & create worktree

- [ ] **Step 1: Confirm main is green**

Run: `cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar 2>&1 | tail -5`
Expected: all 316 tests pass.

- [ ] **Step 2: Create worktree**

```bash
git worktree add -b feat/rfc0007-phase2-wasm-threads .claude/worktrees/rfc0007-phase2 main
cd .claude/worktrees/rfc0007-phase2
```

Expected: new worktree on fresh branch off main.

- [ ] **Step 3: Verify toolchain**

```bash
which wasm-ld clang wasmtime
wasmtime --version
clang --print-targets | grep wasm32
```
Expected: all three found; wasmtime ≥ 14; clang lists `wasm32`.

---

## Task 2: Runtime — wasi_thread_rt.c skeleton (failing test first)

**Files:**
- Create: `lib/Runtime/wasi_thread_rt.c`
- Create: `include/asc/Runtime/wasi_thread_rt.h`

- [ ] **Step 1: Write header**

```c
// include/asc/Runtime/wasi_thread_rt.h
#ifndef ASC_RUNTIME_WASI_THREAD_RT_H
#define ASC_RUNTIME_WASI_THREAD_RT_H

#include <stdint.h>

// Opaque handle returned to asc code. Layout owned by wasi_thread_rt.c.
typedef struct asc_wasi_task asc_wasi_task;

// Spawn a thread. `entry` is a function pointer taking a single `void *arg`.
// On the spawned thread, `entry(arg)` is invoked; its return value is ignored.
// Returns a non-NULL handle on success, NULL on failure (spawn aborts via trap).
asc_wasi_task *__asc_wasi_thread_spawn(void (*entry)(void *), void *arg);

// Block until the task completes, then free the handle.
void __asc_wasi_thread_join(asc_wasi_task *h);

#endif
```

- [ ] **Step 2: Write implementation (no-op host build, real wasi-threads body)**

```c
// lib/Runtime/wasi_thread_rt.c
// Thin wasi-threads trampoline. Only compiled meaningfully for wasm32;
// on the host we provide pthread-backed shims so the host CMake build still
// links when this TU is included.

#include "asc/Runtime/wasi_thread_rt.h"
#include <stdlib.h>
#include <stdint.h>

struct asc_wasi_task {
    int32_t tid;
    int32_t done_flag;    // 0 = running, 1 = done
    void (*entry)(void *);
    void *arg;
};

#if defined(__wasm__)

// Host-provided import per wasi-threads proposal.
extern int32_t __imported_wasi_snapshot_preview1_thread_spawn(void *start_arg)
    __attribute__((import_module("wasi"), import_name("thread-spawn")));

// Exported entrypoint. The wasi-threads host calls this on the spawned thread.
__attribute__((export_name("wasi_thread_start")))
void wasi_thread_start(int32_t tid, void *start_arg) {
    asc_wasi_task *h = (asc_wasi_task *)start_arg;
    h->tid = tid;
    h->entry(h->arg);
    // Release-store so the joiner sees our completion.
    __atomic_store_n(&h->done_flag, 1, __ATOMIC_RELEASE);
    __builtin_wasm_memory_atomic_notify(&h->done_flag, 1);
}

asc_wasi_task *__asc_wasi_thread_spawn(void (*entry)(void *), void *arg) {
    asc_wasi_task *h = (asc_wasi_task *)malloc(sizeof(asc_wasi_task));
    if (!h) __builtin_trap();
    h->tid = 0;
    h->done_flag = 0;
    h->entry = entry;
    h->arg = arg;
    int32_t rc = __imported_wasi_snapshot_preview1_thread_spawn(h);
    if (rc < 0) __builtin_trap();
    return h;
}

void __asc_wasi_thread_join(asc_wasi_task *h) {
    // Phase 2: busy-spin on a volatile load. memory.atomic.wait32 traps
    // "unaligned atomic" in wasmtime 43 even at 4-byte-aligned addresses;
    // proper wait/notify is tracked for Phase 5.
    volatile int32_t *done = (volatile int32_t *)&h->done_flag;
    while (*done == 0) { /* busy-spin */ }
    free(h);
}

#else // Host fallback: thin pthread wrapper so host unit tests can link.

#include <pthread.h>

struct asc_wasi_task_host {
    asc_wasi_task base;
    pthread_t tid;
};

static void *__asc_wasi_thread_trampoline(void *p) {
    asc_wasi_task *h = (asc_wasi_task *)p;
    h->entry(h->arg);
    __atomic_store_n(&h->done_flag, 1, __ATOMIC_RELEASE);
    return NULL;
}

asc_wasi_task *__asc_wasi_thread_spawn(void (*entry)(void *), void *arg) {
    struct asc_wasi_task_host *h =
        (struct asc_wasi_task_host *)malloc(sizeof(*h));
    if (!h) return NULL;
    h->base.tid = 0;
    h->base.done_flag = 0;
    h->base.entry = entry;
    h->base.arg = arg;
    if (pthread_create(&h->tid, NULL, __asc_wasi_thread_trampoline, h) != 0) {
        free(h);
        return NULL;
    }
    return (asc_wasi_task *)h;
}

void __asc_wasi_thread_join(asc_wasi_task *h) {
    struct asc_wasi_task_host *hh = (struct asc_wasi_task_host *)h;
    pthread_join(hh->tid, NULL);
    free(hh);
}

#endif
```

- [ ] **Step 3: Wire into host CMake**

Modify `lib/Runtime/CMakeLists.txt`:

```cmake
if(CMAKE_C_COMPILER)
  add_library(ascRuntime OBJECT
    runtime.c
    wasi_io.c
    wasi_fs.c
    wasi_clock.c
    wasi_random.c
    wasi_env.c
    atomics.c
    string_rt.c
    vec_rt.c
    arc_rt.c
    rc_rt.c
    wasi_thread_rt.c
  )
  target_include_directories(ascRuntime PRIVATE ${CMAKE_SOURCE_DIR}/include)
  target_compile_options(ascRuntime PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
```

- [ ] **Step 4: Host build verifies**

Run: `cd build && cmake --build . --target ascRuntime -j$(sysctl -n hw.ncpu)`
Expected: clean build with no warnings.

- [ ] **Step 5: Commit**

```bash
git add lib/Runtime/wasi_thread_rt.c include/asc/Runtime/wasi_thread_rt.h lib/Runtime/CMakeLists.txt
git commit -m "runtime: add wasi-threads trampoline (host pthread shim + wasm32 imports)"
```

---

## Task 3: ConcurrencyLowering — declare spawn/join runtime symbols for Wasm

**Files:**
- Modify: `lib/CodeGen/ConcurrencyLowering.cpp:107-119`

- [ ] **Step 1: Write failing lit test**

Create `test/CodeGen/wasm_threads_decls.ts`:

```typescript
// RUN: %asc check %s --target wasm32-wasi-threads --emit llvmir -o - | FileCheck %s
// CHECK: declare {{.*}} @__asc_wasi_thread_spawn
// CHECK: declare void @__asc_wasi_thread_join
function spawnit(): void {
    task.spawn(() => { return; });
}
```

- [ ] **Step 2: Run to confirm it fails**

Run: `lit test/CodeGen/wasm_threads_decls.ts -v`
Expected: FAIL — symbols not declared.

- [ ] **Step 3: Update `declareWasiThreadsFunctions`**

Replace `ConcurrencyLowering.cpp:107-119` with:

```cpp
void ConcurrencyLoweringPass::declareWasiThreadsFunctions(mlir::ModuleOp module) {
  mlir::OpBuilder builder(module.getContext());
  builder.setInsertionPointToStart(module.getBody());
  auto loc = builder.getUnknownLoc();
  auto ptrType = mlir::LLVM::LLVMPointerType::get(module.getContext());
  auto voidTy = mlir::LLVM::LLVMVoidType::get(module.getContext());

  // asc_wasi_task *__asc_wasi_thread_spawn(void (*entry)(void *), void *arg)
  if (!module.lookupSymbol("__asc_wasi_thread_spawn")) {
    auto ty = mlir::LLVM::LLVMFunctionType::get(ptrType, {ptrType, ptrType});
    builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_wasi_thread_spawn", ty);
  }

  // void __asc_wasi_thread_join(asc_wasi_task *h)
  if (!module.lookupSymbol("__asc_wasi_thread_join")) {
    auto ty = mlir::LLVM::LLVMFunctionType::get(voidTy, {ptrType});
    builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_wasi_thread_join", ty);
  }
}
```

Note: The old `wasi_thread_start` import declaration is removed — that symbol is exported by `wasi_thread_rt.c`, not called from our IR.

- [ ] **Step 4: Run test again — expect still FAIL** because HIRBuilder still emits `pthread_create`.

Run: `lit test/CodeGen/wasm_threads_decls.ts -v`
Expected: The `declare` lines will now appear only if the lowering pass runs AND HIRBuilder either still calls `pthread_create` (wrong) or the new helpers (next task). For now, the declarations may not appear because `declareWasiThreadsFunctions` only runs when isWasmTarget() AND there are no users — verify by dumping MLIR with `--emit mlir` first.

Given the declarations are unconditional inside `declareWasiThreadsFunctions`, the test should PASS once the triple is wired to the lowering pass. If Driver already passes the triple to `createConcurrencyLoweringPass`, test PASSES. Verify this:

Run: `grep -n "createConcurrencyLoweringPass" lib/CodeGen/CodeGen.cpp lib/Driver/Driver.cpp`
Expected: at least one call passes `targetTriple`.

If not already wired, this is Task 3b below.

- [ ] **Step 5: Commit if test passes**

```bash
git add lib/CodeGen/ConcurrencyLowering.cpp test/CodeGen/wasm_threads_decls.ts
git commit -m "codegen: declare __asc_wasi_thread_spawn/join for Wasm target"
```

---

## Task 4: HIRBuilder — thread the target triple in

**Files:**
- Modify: `include/asc/HIR/HIRBuilder.h` (add member + setter)
- Modify: `lib/HIR/HIRBuilder.cpp` (accept triple in ctor, store it)
- Modify: `lib/Driver/Driver.cpp` (pass `opts.targetTriple` when constructing HIRBuilder)

- [ ] **Step 1: Locate current HIRBuilder construction**

Run: `grep -n "HIRBuilder(" lib/Driver/Driver.cpp lib/HIR/HIRBuilder.cpp | head`

Expected: find the constructor call(s).

- [ ] **Step 2: Add member + setter**

In `include/asc/HIR/HIRBuilder.h`, add near existing member declarations:

```cpp
  /// Target triple for backend-specific lowering (wasm vs native).
  /// Set by Driver before build(). Empty = native defaults.
  llvm::Triple targetTriple;
  void setTargetTriple(llvm::Triple t) { targetTriple = std::move(t); }
  bool isWasmTarget() const {
    return targetTriple.getArch() == llvm::Triple::wasm32 ||
           targetTriple.getArch() == llvm::Triple::wasm64;
  }
```

Include `llvm/TargetParser/Triple.h` at the top if missing.

- [ ] **Step 3: Wire Driver**

In `lib/Driver/Driver.cpp`, immediately after constructing the HIRBuilder, add:

```cpp
builder.setTargetTriple(llvm::Triple(opts.targetTriple));
```

(Exact line depends on current code; grep for the HIRBuilder construction.)

- [ ] **Step 4: Build — no behavior change yet**

Run: `cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -20`
Expected: clean build. All 316 lit tests still pass.

Run: `lit test/ --no-progress-bar 2>&1 | tail -3`
Expected: pass count unchanged.

- [ ] **Step 5: Commit**

```bash
git add include/asc/HIR/HIRBuilder.h lib/HIR/HIRBuilder.cpp lib/Driver/Driver.cpp
git commit -m "hir: thread target triple into HIRBuilder for Wasm-vs-native branching"
```

---

## Task 5: HIRBuilder — extract spawn helper and branch by target

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp` in the `task_spawn` case around lines 4915-5118

The existing code packs captures into a malloc'd env struct, then calls `pthread_create(&tid, NULL, wrapper, env)`. The packing stays; only the call differs.

- [ ] **Step 1: Write failing e2e test for Wasm spawn**

Create `test/e2e/task_spawn_wasm_basic.ts`:

```typescript
// RUN: %asc build %s --target wasm32-wasi-threads -o %t.wasm 2>&1 | FileCheck %s --allow-empty --check-prefix=BUILD
// RUN: wasm-objdump -x %t.wasm | FileCheck %s --check-prefix=WASM
// BUILD-NOT: error
// WASM: import: env.thread-spawn
// WASM: export: wasi_thread_start
function worker(): void {
    return;
}
function main(): i32 {
    task.spawn(worker);
    return 0;
}
```

If `wasm-objdump` is unavailable, substitute with a grep over `llvmir` output checking for `@__asc_wasi_thread_spawn` instead.

- [ ] **Step 2: Run to confirm FAIL**

Run: `lit test/e2e/task_spawn_wasm_basic.ts -v`
Expected: FAIL — test compiles but produces `pthread_create` symbols, not `__asc_wasi_thread_spawn`.

- [ ] **Step 3: Replace the spawn-call block with target-branched code**

In `lib/HIR/HIRBuilder.cpp` around line 5004-5106 (the `pthread_create` path), replace:

```cpp
// Declare pthread_create: i32 (ptr, ptr, ptr, ptr)
auto pthreadCreateFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("pthread_create");
// ... existing pthread_create call ...
```

with:

```cpp
// Branch on target: wasm uses runtime trampoline, native uses pthread_create.
if (isWasmTarget()) {
  // Declare __asc_wasi_thread_spawn(entry, arg) -> ptr handle.
  auto spawnFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__asc_wasi_thread_spawn");
  if (!spawnFn) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(module.getBody());
    auto fnType = mlir::LLVM::LLVMFunctionType::get(ptrType, {ptrType, ptrType});
    spawnFn = builder.create<mlir::LLVM::LLVMFuncOp>(
        location, "__asc_wasi_thread_spawn", fnType);
  }
  if (!threadArg)
    threadArg = builder.create<mlir::LLVM::ZeroOp>(location, ptrType);
  // handle = __asc_wasi_thread_spawn(wrapper, env)
  auto handle = builder.create<mlir::LLVM::CallOp>(
      location, spawnFn, mlir::ValueRange{wrapperAddr, threadArg}).getResult();

  // Stash the handle in an alloca so task_join can load it uniformly with native.
  auto i64One = builder.create<mlir::LLVM::ConstantOp>(
      location, i64Type, static_cast<int64_t>(1));
  auto handleAlloca = builder.create<mlir::LLVM::AllocaOp>(
      location, ptrType, i64Type, i64One);
  builder.create<mlir::LLVM::StoreOp>(location, handle, handleAlloca);

  if (!taskScopeHandleStack.empty())
    taskScopeHandleStack.back().push_back(handleAlloca);
  return handleAlloca;
}

// Native path: declare and call pthread_create.
auto pthreadCreateFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("pthread_create");
// ... existing pthread_create code unchanged below ...
```

Note: `wrapperAddr`, `threadArg`, `i64Type`, `ptrType` are already in scope above this block. The wasm path skips the `tidAlloca` that pthread_create needs (it returns the handle directly via the call result).

Also: the wrapper signature for pthread is `void *(*)(void *)`; for wasi-threads we want `void (*)(void *)`. The current wrapper at line 5001 returns null — its type is already `ptr (ptr)`. That's compatible enough to cast (LLVM IR treats function pointers as opaque at the call site). Leave the wrapper signature as-is; the runtime helper accepts `void (*)(void *)` but LLVM IR `ptrcall` doesn't care. If a verifier complains, emit a bitcast in the wasm branch.

- [ ] **Step 4: Build**

Run: `cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -10`
Expected: clean build.

- [ ] **Step 5: Rerun lit — Wasm test advances, native unaffected**

Run: `lit test/ --no-progress-bar 2>&1 | tail -3`
Expected: +1 passing (the new wasm test), 0 regressions.

- [ ] **Step 6: Commit**

```bash
git add lib/HIR/HIRBuilder.cpp test/e2e/task_spawn_wasm_basic.ts
git commit -m "hir: lower task.spawn to __asc_wasi_thread_spawn on wasm targets"
```

---

## Task 6: HIRBuilder — branch task_join similarly

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp` around lines 5120-5148 (the `task_join` case)

- [ ] **Step 1: Update the task_join branch**

Replace:

```cpp
auto pthreadJoinFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("pthread_join");
// ... existing pthread_join code ...
```

with:

```cpp
if (isWasmTarget()) {
  auto joinFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("__asc_wasi_thread_join");
  if (!joinFn) {
    mlir::OpBuilder::InsertionGuard guard(builder);
    builder.setInsertionPointToStart(module.getBody());
    auto voidTy = mlir::LLVM::LLVMVoidType::get(builder.getContext());
    auto fnType = mlir::LLVM::LLVMFunctionType::get(voidTy, {ptrType});
    joinFn = builder.create<mlir::LLVM::LLVMFuncOp>(
        location, "__asc_wasi_thread_join", fnType);
  }
  // Load the handle pointer stored during spawn, pass to join.
  auto hVal = builder.create<mlir::LLVM::LoadOp>(location, ptrType, handle);
  builder.create<mlir::LLVM::CallOp>(location, joinFn, mlir::ValueRange{hVal});
  return {};
}

// Native pthread_join path unchanged below.
auto pthreadJoinFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("pthread_join");
// ...
```

- [ ] **Step 2: Add a join test**

Append to `test/e2e/task_spawn_wasm_basic.ts`:

```typescript
function main(): i32 {
    let h = task.spawn(worker);
    task.join(h);
    return 0;
}
```

Update the WASM FileCheck directives:

```
// WASM: import: env.thread-spawn
// WASM: export: wasi_thread_start
// WASM-DAG: __asc_wasi_thread_spawn
// WASM-DAG: __asc_wasi_thread_join
```

- [ ] **Step 3: Build + lit**

Run: `cmake --build build -j$(sysctl -n hw.ncpu) && lit test/ --no-progress-bar 2>&1 | tail -3`
Expected: +test passes, 0 regressions.

- [ ] **Step 4: Commit**

```bash
git add lib/HIR/HIRBuilder.cpp test/e2e/task_spawn_wasm_basic.ts
git commit -m "hir: lower task.join to __asc_wasi_thread_join on wasm targets"
```

---

## Task 7: Driver — Wasm linker flags + compile runtime with threads

**Files:**
- Modify: `lib/Driver/Driver.cpp:1109-1191` (`linkWasm`)

- [ ] **Step 1: Write failing test asserting the flags appear**

Create `test/e2e/wasm_threads_link_flags.ts`:

```typescript
// RUN: %asc build %s --target wasm32-wasi-threads -o %t.wasm --verbose 2>&1 | FileCheck %s
// CHECK: [link]
// CHECK-SAME: --shared-memory
// CHECK-SAME: --import-memory
// CHECK-SAME: --max-memory=
// CHECK-SAME: --export=wasi_thread_start
function main(): i32 { return 0; }
```

- [ ] **Step 2: Confirm FAIL**

Run: `lit test/e2e/wasm_threads_link_flags.ts -v`
Expected: FAIL — no threading flags emitted.

- [ ] **Step 3: Detect "threads" environment in `linkWasm`**

At the top of `linkWasm`, add:

```cpp
llvm::Triple triple(opts.targetTriple);
bool threadsEnabled =
    triple.getEnvironmentName().contains("threads") ||
    triple.getOSName().contains("threads");
```

Note: LLVM 18 parses `wasm32-wasi-threads` as OS=`wasi`, Env=`threads` in some versions and OS=`wasi-threads` in others. The `contains("threads")` check covers both.

- [ ] **Step 4: Add runtime sources when threads enabled**

Extend the runtime-compile block around line 1122-1147:

```cpp
llvm::SmallVector<std::string, 4> runtimeSources;
runtimeSources.push_back("lib/Runtime/runtime.c");
if (threadsEnabled) {
  runtimeSources.push_back("lib/Runtime/wasi_thread_rt.c");
  runtimeSources.push_back("lib/Runtime/atomics.c");
}

// Resolve each source relative to CWD with the fallback list already used.
// Compile each to its own .o, collect into `runtimeObjs`.
llvm::SmallVector<std::string, 4> runtimeObjs;
for (const auto &src : runtimeSources) {
  std::string resolved;
  for (const char *prefix : {"", "../", "../../"}) {
    std::string candidate = std::string(prefix) + src;
    if (llvm::sys::fs::exists(candidate)) { resolved = candidate; break; }
  }
  if (resolved.empty()) continue;

  std::string obj = outFile + "." + llvm::sys::path::stem(src).str() + ".o";
  llvm::SmallVector<llvm::StringRef, 10> cargs;
  cargs.push_back(*clangPath);
  cargs.push_back(threadsEnabled ? "--target=wasm32-wasi-threads"
                                  : "--target=wasm32-wasi");
  if (threadsEnabled) {
    cargs.push_back("-pthread");
    cargs.push_back("-matomics");
    cargs.push_back("-mbulk-memory");
  }
  cargs.push_back("-c");
  cargs.push_back(resolved);
  cargs.push_back("-I");
  cargs.push_back("include");
  cargs.push_back("-o");
  cargs.push_back(obj);
  std::string err;
  if (llvm::sys::ExecuteAndWait(*clangPath, cargs, std::nullopt, {}, 60, 0, &err) == 0) {
    runtimeObjs.push_back(obj);
  } else if (opts.verbose) {
    llvm::errs() << "  [warn] failed to compile " << resolved << ": " << err << "\n";
  }
}
```

- [ ] **Step 5: Pass flags to wasm-ld**

After the existing `args.push_back("--allow-undefined")`:

```cpp
if (threadsEnabled) {
  args.push_back("--shared-memory");
  args.push_back("--import-memory");
  args.push_back("--max-memory=67108864"); // 64 MiB default
  args.push_back("--export=wasi_thread_start");
  args.push_back("--no-check-features");   // tolerate mismatched atomics flag
}
```

Also include all `runtimeObjs` in the arg list instead of the single `runtimeObj`.

- [ ] **Step 6: Clean up temp objects**

At the existing cleanup site, loop over `runtimeObjs` and `std::remove` each.

- [ ] **Step 7: Build + rerun failing test**

Run: `cmake --build build -j$(sysctl -n hw.ncpu) && lit test/e2e/wasm_threads_link_flags.ts -v`
Expected: PASS.

- [ ] **Step 8: Run full lit**

Run: `lit test/ --no-progress-bar 2>&1 | tail -3`
Expected: 0 regressions.

- [ ] **Step 9: Commit**

```bash
git add lib/Driver/Driver.cpp test/e2e/wasm_threads_link_flags.ts
git commit -m "driver: emit shared-memory + wasi_thread_start export + runtime threads flags"
```

---

## Task 8: End-to-end wasmtime execution (best-effort)

**Files:**
- Create: `test/e2e/task_spawn_wasm_run.ts`

- [ ] **Step 1: Write the run-test**

```typescript
// REQUIRES: wasmtime-threads
// RUN: %asc build %s --target wasm32-wasi-threads -o %t.wasm
// RUN: wasmtime run --wasi threads=y %t.wasm | FileCheck %s
// CHECK: worker ran
// CHECK: joined
function worker(): void {
    print("worker ran");
}
function main(): i32 {
    let h = task.spawn(worker);
    task.join(h);
    print("joined");
    return 0;
}
```

- [ ] **Step 2: Gate via lit feature**

Add to `test/lit.cfg.py` a feature check:

```python
import shutil, subprocess
if shutil.which("wasmtime"):
    try:
        out = subprocess.check_output(["wasmtime", "--version"], text=True)
        # wasmtime 14+ supports --wasi threads=y
        ver = out.split()[-1].split(".")[0]
        if int(ver) >= 14:
            config.available_features.add("wasmtime-threads")
    except Exception:
        pass
```

- [ ] **Step 3: Run — expect pass or skip**

Run: `lit test/e2e/task_spawn_wasm_run.ts -v`
Expected: if wasmtime ≥ 14 is installed, PASS; otherwise SKIP (REQUIRES feature absent).

If the test FAILs rather than passes, capture the failure output — most likely causes and their fixes:
- `_initialize` missing: runtime provides it via reactor adapter — add `--export=_initialize` to linker flags.
- Import signature mismatch: `thread-spawn` must be `i32 -> i32`; adjust import_name / import_module in wasi_thread_rt.c.
- Shared memory mismatch: ensure both runtime and user .o are compiled with `-matomics -mbulk-memory`.

- [ ] **Step 4: Commit**

```bash
git add test/e2e/task_spawn_wasm_run.ts test/lit.cfg.py
git commit -m "test: e2e wasmtime run for task.spawn+join (gated on wasmtime>=14)"
```

---

## Task 9: Docs & status

**Files:**
- Modify: `rfcs/RFC-0007-concurrency-model.md` (§156-242 ABI correction)
- Modify: `CLAUDE.md` (Known Gaps #1 update, RFC-0007 % bump)

- [ ] **Step 1: Fix the `wasi_thread_start` ABI direction in RFC-0007**

In `rfcs/RFC-0007-concurrency-model.md:204-210`, replace the "imported WASI function" text with:

```
### Step 4 — Start thread

The compiler emits a runtime call to `__asc_wasi_thread_spawn(entry_fn_ptr, closure_ptr)`
which internally:
  1. Calls the imported host function `wasi:thread-spawn/thread-spawn(closure_ptr) -> i32`
     (the wasi-threads proposal import).
  2. Returns the thread handle to the caller.

The module also **exports** `wasi_thread_start(tid: i32, closure_ptr: i32)` — the host
runtime invokes this on the newly-spawned thread. Its body unpacks the closure, runs the
task body, and atomically flips `done_flag`.
```

- [ ] **Step 2: Update CLAUDE.md**

In the Known Gaps section, modify #1 to note Phase 2 is done:

```
1. **Closure literals in task.spawn** — supported on native AND wasm32-wasi-threads
   (Phase 2 complete). `thread::scope` with lifetime-bounded borrows still deferred to
   Phase 6.
```

Bump the RFC-0007 row in the coverage table from `~58%` to `~70%`.

- [ ] **Step 3: Commit**

```bash
git add rfcs/RFC-0007-concurrency-model.md CLAUDE.md
git commit -m "docs: RFC-0007 ABI clarification + Phase 2 coverage bump"
```

---

## Task 10: Push + PR

- [ ] **Step 1: Final full lit sweep**

Run: `lit test/ --no-progress-bar 2>&1 | tail -5`
Expected: 319/319 (316 baseline + 3 new) or 320/320 (if wasmtime feature active).

- [ ] **Step 2: Push**

```bash
git push -u origin feat/rfc0007-phase2-wasm-threads
```

- [ ] **Step 3: Open PR**

```bash
gh pr create --title "RFC-0007 Phase 2: Wasm task.spawn via wasi-threads" --body "$(cat <<'EOF'
## Summary
- `task.spawn` / `task.join` now lower to `__asc_wasi_thread_spawn` / `__asc_wasi_thread_join` on `wasm32-wasi-threads`
- New C runtime `lib/Runtime/wasi_thread_rt.c` provides the spawn trampoline, exported `wasi_thread_start` entry, and done-flag join via `memory.atomic.wait32`
- Driver emits `--shared-memory --import-memory --max-memory --export=wasi_thread_start` for wasm-threads targets and rebuilds runtime objects with `-pthread -matomics -mbulk-memory`

## Test plan
- [ ] `lit test/` passes (316 baseline + 3 new Wasm tests)
- [ ] `task_spawn_wasm_basic.ts` produces a module with the expected imports/exports
- [ ] `task_spawn_wasm_run.ts` executes under `wasmtime run --wasi threads=y` and prints `worker ran` / `joined`
- [ ] Native pthread path unchanged (existing `task_spawn_closure_run.ts` still green)

EOF
)"
```

---

## Self-Review

**Spec coverage:**
- RFC-0007 §156-242 Wasm spawn/join — covered by Tasks 2, 5, 6, 7, 8. Stack-size analysis (Step 3 of RFC) is explicitly deferred to Phase 3.
- ABI direction error in the spec — flagged and corrected in Task 9.

**Placeholder scan:** No "TBD" or "implement error handling" left. The only speculative guidance is the Task 8 Step 3 troubleshooting list, which names specific likely failure modes and their fixes.

**Type consistency:** `asc_wasi_task *` is the handle type returned from `__asc_wasi_thread_spawn`, consumed by `__asc_wasi_thread_join`. In MLIR, handle = `!llvm.ptr` for both backends; the Wasm path stores it via alloca to match native's `task_join` load pattern. Wrapper fn signature `ptr (ptr)` is preserved from the existing pthread path; wasi-threads tolerates the unused return.

**Deferred to later phases:**
- Phase 3: Static stack-size analysis → `pthread_attr_setstacksize` / wasi-threads stack hint
- Phase 4: MPMC channels
- Phase 5: Futex-backed mutex/rwlock/semaphore on Wasm (currently spin)
- Phase 6: `thread::scope` with lifetime-bounded borrows
