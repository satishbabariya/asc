# Thread Lifecycle End-to-End: RFC-0007/0008/0009 Correctness Push

| Field | Value |
|---|---|
| Date | 2026-04-16 |
| Goal | Fix critical panic/memory bugs, wire dead flags, add catch_unwind — make threads work safely end-to-end |
| Baseline | 249/249 tests, RFC-0007 40%, RFC-0008 55%, RFC-0009 45% |
| Target | RFC-0007 ~48%, RFC-0008 ~68%, RFC-0009 ~65% |

## Motivation

The fresh audit (2026-04-16) and deep code exploration revealed that the thread lifecycle — spawn, run, panic, join — has three categories of issues:

1. **Memory corruption bug**: PanicLowering.cpp:174-178 calls `__drop_TypeName` unconditionally in cleanup blocks without checking drop flags. If a struct value was moved before the panic, the destructor runs on moved-from memory — double-free or use-after-free.

2. **Dead flags**: `--no-panic-unwind` is forwarded to `CodeGenOptions` (fixed in PR #35) but `PanicScopeWrap` never reads it — the flag has zero effect. `--max-threads` is similarly forwarded but unused for arena sizing.

3. **Missing APIs**: No `catch_unwind` for user-level panic recovery. OOM silently returns NULL. Channel slot destructors missing in C runtime path.

### Corrected findings from audit

Items the audit incorrectly reported as missing that are actually implemented:
- **task.join IS lowered** — both explicit `task_join` (HIRBuilder.cpp:4896-4921) and automatic join-all via `TaskScopeExpr` (HIRBuilder.cpp:4931-4967)
- **Escape analysis IS wired** — runs at Driver.cpp:998, sets `escape_status` attribute, OwnershipLowering.cpp:107 reads it
- **Channel Receiver Drop already drops items** — std/thread/channel.ts:244-258 iterates remaining items and calls `ptr_drop_in_place`

## Phase 1: Fix Panic Cleanup Drop Flags (Critical)

### The Bug

`lib/CodeGen/PanicLowering.cpp` lines 174-178:

```cpp
for (auto &dt : dropTargets) {
    auto dropFn = module.lookupSymbol<mlir::func::FuncOp>(dt.dropName);
    if (dropFn)
      builder.create<mlir::func::CallOp>(loc, dropFn,
          mlir::ValueRange{dt.ptr});  // ← NO FLAG CHECK
}
```

This calls `__drop_TypeName` unconditionally for every struct alloca. But when a value has been conditionally moved, a drop flag (`own.drop_flag_alloc`) tracks whether the value still needs dropping. The normal drop path in `OwnershipLowering.cpp:297-322` correctly checks these flags via `load flag → cond_br`. The panic cleanup path does not.

### The Fix

In PanicLowering.cpp, when building the cleanup block, for each `DropTarget`:

1. Search the function's entry block for an `own.drop_flag_alloc` op whose result is associated with the same struct alloca (by scanning uses of the alloca for `own.drop_flag_set` ops, then tracing back to the flag alloca)
2. If a drop flag exists: emit `load i1 flag → cond_br → drop_block (call __drop + br merge) → merge_block`
3. If no drop flag exists: call `__drop_TypeName` unconditionally (same as current behavior — values without conditional moves don't have flags)

**Association strategy:** PanicLowering runs BEFORE OwnershipLowering. At this point, `own.drop` ops still exist in the normal-exit path (they get lowered to `free` calls later). These `own.drop` ops carry a `drop_flag` attribute (set by DropInsertion at DropInsertion.cpp:255) and have the flag value as their second operand.

For each struct alloca in the function, scan for `own.drop` ops that reference it. If the `own.drop` has a `drop_flag` attribute and a second operand, that second operand is the `own.drop_flag_alloc` result — the i1 flag pointer.

Alternatively, scan the entry block for `own.drop_flag_alloc` ops. Each produces an i1 result. Map these to struct allocas by checking which `own.drop_flag_set` ops reference both the flag and the struct value.

**Modified struct:**
```cpp
struct DropTarget {
    mlir::Value ptr;
    std::string dropName;
    mlir::Value dropFlag;  // nullptr if no conditional move
};
```

**Cleanup block generation changes to:**
```cpp
for (auto &dt : dropTargets) {
    auto dropFn = module.lookupSymbol<mlir::func::FuncOp>(dt.dropName);
    if (!dropFn) continue;

    if (dt.dropFlag) {
        // Conditional drop: check flag before calling destructor.
        auto i1Ty = mlir::IntegerType::get(ctx, 1);
        auto flagVal = builder.create<mlir::LLVM::LoadOp>(loc, i1Ty, dt.dropFlag);

        mlir::Block *dropBlock = new mlir::Block();
        mlir::Block *mergeBlock = new mlir::Block();
        funcOp.getBody().getBlocks().insertAfter(
            mlir::Region::iterator(cleanupBlock), dropBlock);
        funcOp.getBody().getBlocks().insertAfter(
            mlir::Region::iterator(dropBlock), mergeBlock);

        builder.create<mlir::LLVM::CondBrOp>(loc, flagVal, dropBlock, mergeBlock);

        builder.setInsertionPointToStart(dropBlock);
        builder.create<mlir::func::CallOp>(loc, dropFn, mlir::ValueRange{dt.ptr});
        builder.create<mlir::LLVM::BrOp>(loc, mlir::ValueRange{}, mergeBlock);

        builder.setInsertionPointToStart(mergeBlock);
    } else {
        // Unconditional drop.
        builder.create<mlir::func::CallOp>(loc, dropFn, mlir::ValueRange{dt.ptr});
    }
}
```

**Files:** `lib/CodeGen/PanicLowering.cpp`
**LOC:** ~60

### Validation

- All 249 existing tests must pass (no test should have been relying on the double-drop)
- Add `test/e2e/panic_drop_flag.ts` — a test where a value is conditionally moved, then a panic occurs. With the fix, the destructor should NOT run on the moved value. Without the fix, it would double-free.

---

## Phase 2: Wire --no-panic-unwind to PanicScopeWrap

### Current State

`PanicScopeWrap::runOnOperation()` (PanicScopeWrap.cpp:316-346) always runs scope identification and wrapping. It reads `llvm.target_triple` to choose Wasm vs native EH, but never checks for a panic-unwind opt-out.

### The Fix

**Step 1:** In `lib/Driver/Driver.cpp`, in `lowerToHIR()` or `runAnalysis()`, after the MLIR module is created, set a module attribute when the flag is true:

```cpp
if (opts.noPanicUnwind) {
    mlirState->module->setAttr("asc.no_panic_unwind",
        mlir::BoolAttr::get(&mlirState->context, true));
}
```

Find the exact location by reading where `llvm.target_triple` is set — the `noPanicUnwind` attribute should be set in the same place.

**Step 2:** In `PanicScopeWrap::runOnOperation()` (PanicScopeWrap.cpp:316), add an early exit:

```cpp
void PanicScopeWrapPass::runOnOperation() {
    mlir::func::FuncOp func = getOperation();
    if (func.isDeclaration())
        return;

    // Check for --no-panic-unwind opt-out.
    if (auto moduleOp = func->getParentOfType<mlir::ModuleOp>()) {
        if (moduleOp->hasAttr("asc.no_panic_unwind"))
            return;  // Skip all panic scope wrapping — panics trap directly.
    }

    // ... rest of existing code
```

When scope wrapping is skipped, `__asc_panic()` in runtime.c triggers `__builtin_trap()` (Wasm) or `abort()` (native) because no handler is registered via setjmp — this is the correct behavior for `--no-panic-unwind`.

**Files:** `lib/Driver/Driver.cpp`, `lib/Analysis/PanicScopeWrap.cpp`
**LOC:** ~20

### Validation

- All 249 tests must pass (tests don't use `--no-panic-unwind`)
- Add `test/e2e/no_panic_unwind.ts` — a test that uses `--no-panic-unwind` flag and verifies compilation succeeds without try/catch scope ops in the output (use `--emit mlir` and check for absence of `own.try_scope`)

---

## Phase 3: OOM Trap in Arena Allocator

### Current State

`lib/Runtime/runtime.c:77` — `__asc_arena_alloc` returns `0` (NULL) when arena is exhausted:
```c
if (result + size > __asc_arena_end) return 0;
```

No caller checks the return value. A NULL pointer is used as if it were a valid allocation.

### The Fix

Replace the NULL return with a panic call:

```c
if (result + size > __asc_arena_end) {
    __asc_panic("arena allocation failed: out of memory", 41,
                "runtime.c", 9, __LINE__, 0);
    __builtin_unreachable();  // __asc_panic does not return
}
```

On Wasm without panic handler: triggers `unreachable` trap (the standard Wasm OOM behavior).
On native: prints the panic message and aborts.
With catch_unwind (Phase 5): OOM becomes a catchable panic.

**Files:** `lib/Runtime/runtime.c`
**LOC:** ~10

### Validation

- All 249 tests must pass (none currently exhaust the arena)
- The change is safe because `__asc_panic` already exists and is correctly wired

---

## Phase 4: Arena Sizing from --max-threads

### Current State

`lib/Runtime/runtime.c:52` — `ASC_DEFAULT_ARENA_SIZE` is 1 MB constant.
`CodeGenOptions.maxThreads` is set to 4 (or user value) but unused.

### The Fix

**Step 1:** In `lib/Runtime/runtime.c`, declare a weak global:

```c
// Weak default — compiler emits a strong definition to override.
__attribute__((weak)) unsigned int __asc_max_threads = 4;
#define PER_THREAD_STACK_SIZE (256 * 1024)  // 256 KB per thread
```

Change arena init to use dynamic sizing:

```c
void __asc_arena_init(void) {
    unsigned long arena_size = (unsigned long)__asc_max_threads * PER_THREAD_STACK_SIZE;
    if (arena_size < ASC_DEFAULT_ARENA_SIZE)
        arena_size = ASC_DEFAULT_ARENA_SIZE;  // Minimum 1 MB
#ifdef __wasm__
    // Wasm: use static buffer (can't dynamically allocate before main).
    // The static buffer is still ASC_DEFAULT_ARENA_SIZE.
    // For Wasm, --max-threads affects thread count, not arena size.
    __asc_arena_ptr = __asc_arena_buf;
    __asc_arena_end = __asc_arena_buf + sizeof(__asc_arena_buf);
#else
    // Native: dynamically allocate arena.
    __asc_arena_ptr = (unsigned char *)malloc(arena_size);
    __asc_arena_end = __asc_arena_ptr + arena_size;
#endif
}
```

**Step 2:** In `lib/CodeGen/CodeGen.cpp`, after `translateToLLVMIR()` creates the LLVM module, emit the strong global:

```cpp
// Emit __asc_max_threads global (overrides weak default in runtime).
if (opts.maxThreads != 4) {  // Only emit if non-default
    auto *maxThreadsGlobal = new llvm::GlobalVariable(
        *llvmModule, llvm::Type::getInt32Ty(llvmContext),
        /*isConstant=*/true, llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvmContext), opts.maxThreads),
        "__asc_max_threads");
    (void)maxThreadsGlobal;
}
```

**Files:** `lib/Runtime/runtime.c`, `lib/CodeGen/CodeGen.cpp`
**LOC:** ~40

### Validation

- All 249 tests pass
- Verify with `--max-threads 8` that compilation succeeds and the global appears in `--emit llvmir`

---

## Phase 5: catch_unwind

### Design

`catch_unwind` is a compiler builtin that wraps a closure in panic-catching logic:

```typescript
const result = catch_unwind(() => {
    // code that might panic
    dangerous_function();
});
match result {
    Result::Ok(v) => { /* success */ },
    Result::Err(info) => { /* panic caught, info has message/file/line */ },
}
```

### Implementation

**Option A (Runtime helper):** Add a C runtime function `__asc_catch_unwind` that:
1. Saves the current panic handler
2. Sets up a new setjmp/longjmp frame
3. Calls the closure function pointer
4. Restores the old handler
5. Returns 0 on success, 1 on panic

This avoids modifying HIRBuilder. The compiler recognizes `catch_unwind(fn)` as a builtin call and emits: `rc = call __asc_catch_unwind(fn_ptr, arg_ptr)`.

**Runtime function:**

```c
int __asc_catch_unwind(void *(*fn)(void *), void *arg, PanicInfo *out_info) {
    // Save current handler.
    jmp_buf *prev = __asc_panic_jmpbuf;
    int prev_unwind = __asc_in_unwind;

    // Set up new handler.
    jmp_buf buf;
    __asc_panic_jmpbuf = &buf;
    __asc_in_unwind = 0;

    if (setjmp(buf) == 0) {
        // Normal path: call the closure.
        fn(arg);
        // Restore previous handler.
        __asc_panic_jmpbuf = prev;
        __asc_in_unwind = prev_unwind;
        return 0;  // Success
    } else {
        // Panic path: longjmp returned here.
        if (out_info) {
            *out_info = __asc_panic_info;
        }
        // Restore previous handler.
        __asc_panic_jmpbuf = prev;
        __asc_in_unwind = prev_unwind;
        return 1;  // Panic caught
    }
}
```

**HIRBuilder recognition:** In the builtin call handling section of HIRBuilder (around line 4695 where `task_spawn` is recognized), add:

```cpp
if (name == "catch_unwind") {
    // Lower to: rc = call __asc_catch_unwind(fn_ptr, null, &panic_info)
    // Then: if rc == 0 { Result::Ok(()) } else { Result::Err(panic_info) }
    // ...
}
```

The function pointer for the closure is obtained the same way as `task_spawn` — via `DeclRefExpr` name lookup and `llvm.addressof`.

**Files:** `lib/Runtime/runtime.c`, `lib/HIR/HIRBuilder.cpp`
**LOC:** ~150

### Validation

- All 249 tests pass
- Add `test/e2e/catch_unwind.ts` — call `catch_unwind` with a panicking closure, verify it returns Err

---

## Phase 6: Channel Slot Destructors in C Runtime

### Current State

The std library's `impl Drop for Receiver<T>` (channel.ts:244-258) already correctly drops remaining items. But the C runtime's `__asc_chan_drop` in `channel_rt.c` doesn't call destructors — it only frees the struct when refcount reaches zero.

### The Fix

The C runtime path is used when channels are lowered inline in HIRBuilder (the `chan.make/send/recv` HIR ops). Add a destructor callback parameter to `__asc_chan_drop`:

```c
void __asc_chan_drop(void *chan, unsigned int elem_size,
                     void (*elem_destructor)(void *)) {
    ChannelHeader *h = (ChannelHeader *)chan;
    int old_ref = __atomic_fetch_sub(&h->ref_count, 1, __ATOMIC_ACQ_REL);
    if (old_ref == 1) {
        // Last reference — drain and destroy remaining elements.
        if (elem_destructor) {
            while (h->head != h->tail) {
                unsigned int idx = h->head % h->capacity;
                void *slot = (unsigned char *)h + sizeof(ChannelHeader) + idx * elem_size;
                elem_destructor(slot);
                h->head++;
            }
        }
        free(h);
    }
}
```

This requires updating HIRBuilder's channel drop lowering to pass the `__drop_TypeName` function pointer (or NULL for types without destructors).

**Files:** `lib/Runtime/channel_rt.c`, `lib/HIR/HIRBuilder.cpp` (channel drop emit)
**LOC:** ~40

### Validation

- All 249 tests pass
- Add `test/e2e/chan_drop_destructor.ts` — send a value with a destructor into a channel, drop the receiver without receiving, verify no leak

---

## Phase 7: Fix in_unwind Thread-Local on Wasm

### Current State

`lib/Runtime/runtime.c:98`:
```c
#ifdef __wasm__
static int __asc_in_unwind = 0;  // NOT thread-local
#else
_Thread_local static int __asc_in_unwind = 0;
```

On Wasm with threads, `static` is shared across all threads. Two threads panicking simultaneously could interfere with each other's double-panic detection.

### The Fix

Use `_Thread_local` on Wasm too when threading is enabled:

```c
#if defined(__wasm__) && defined(__wasm_threads__)
_Thread_local static int __asc_in_unwind = 0;
_Thread_local static jmp_buf *__asc_panic_jmpbuf = 0;
#elif defined(__wasm__)
static int __asc_in_unwind = 0;
#else
_Thread_local static int __asc_in_unwind = 0;
_Thread_local static jmp_buf *__asc_panic_jmpbuf = 0;
#endif
```

The `__wasm_threads__` macro is defined when compiling with `-matomics -mbulk-memory` (which wasm32-wasi-threads implies). `_Thread_local` on Wasm with threads uses the Wasm TLS proposal, which wasm-ld handles via `__tls_base`.

Also apply the same fix to `__asc_panic_info` (line 115) and `__asc_panic_jmpbuf`.

**Files:** `lib/Runtime/runtime.c`
**LOC:** ~15

### Validation

- All 249 tests pass (single-threaded tests are unaffected)
- Correct behavior verified by: double-panic in one thread should not affect another thread's unwind state

---

## Build Sequence

```
Phase 1 (drop flags in cleanup) → rebuild compiler → test
Phase 2 (--no-panic-unwind) → rebuild compiler → test
Phase 3 (OOM trap) → test (runtime only, but tests use compiler)
Phase 4 (arena sizing) → rebuild compiler + runtime → test
Phase 5 (catch_unwind) → rebuild compiler + runtime → test
Phase 6 (channel destructors) → rebuild runtime → test
Phase 7 (TLS fix) → rebuild runtime → test
```

Three compiler rebuilds: after Phase 1, Phase 2, and Phase 4/5. Phases 3, 6, 7 are runtime-only but require relinking.

## Files Modified

**Compiler (C++):**
- `lib/CodeGen/PanicLowering.cpp` — drop flag checks in cleanup block
- `lib/Analysis/PanicScopeWrap.cpp` — read asc.no_panic_unwind attribute
- `lib/Driver/Driver.cpp` — set asc.no_panic_unwind module attribute
- `lib/CodeGen/CodeGen.cpp` — emit __asc_max_threads global
- `lib/HIR/HIRBuilder.cpp` — catch_unwind builtin recognition, channel drop with destructor

**Runtime (C):**
- `lib/Runtime/runtime.c` — OOM trap, arena sizing, TLS fix, catch_unwind function
- `lib/Runtime/channel_rt.c` — element destructor callback in __asc_chan_drop

**Tests (new):**
- `test/e2e/panic_drop_flag.ts`
- `test/e2e/no_panic_unwind.ts`
- `test/e2e/catch_unwind.ts`
- `test/e2e/chan_drop_destructor.ts`

## Estimated LOC

| Phase | LOC |
|-------|-----|
| Phase 1: Drop flag checks | ~60 C++ |
| Phase 2: --no-panic-unwind | ~20 C++ |
| Phase 3: OOM trap | ~10 C |
| Phase 4: Arena sizing | ~40 C++ + C |
| Phase 5: catch_unwind | ~150 C++ + C |
| Phase 6: Channel destructors | ~40 C + C++ |
| Phase 7: TLS fix | ~15 C |
| Tests | ~80 TS |
| **Total** | **~415 LOC** |

## Coverage Impact

| RFC | Before | After | Change |
|-----|--------|-------|--------|
| 0007 Concurrency | 40% | ~48% | +8% |
| 0008 Memory Model | 55% | ~68% | +13% |
| 0009 Panic/Unwind | 45% | ~65% | +20% |
| **Overall** | **~82%** | **~84%** | **+2%** |

## What This Does NOT Include

- Wasm EH (throw/catch/rethrow proposal) — uses setjmp/longjmp; Wasm EH is a separate initiative
- thread::scope — requires compiler-level lifetime scoping
- Static stack size computation passed to pthread_attr_setstacksize — deferred
- Win32 concurrency (CreateThread/WaitForSingleObject) — separate platform target work
- memory.grow for dynamic heap expansion — not needed with ownership model
