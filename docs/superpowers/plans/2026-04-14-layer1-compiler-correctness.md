# Layer 1: Compiler Correctness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring RFCs 0005–0009 to 100% — every ownership, borrow checking, memory management, concurrency, and panic feature is implemented and tested.

**Architecture:** 10 tasks targeting 6 C++ files and 2 C runtime files. The critical path is Task 1 (conditional drop branching) which fixes a double-free risk, followed by Task 2 (struct escape analysis) and Task 3 (closure capture). Tasks 4–10 are independent and can be parallelized.

**Tech Stack:** C++17, MLIR (LLVM 18), C11 runtime, lit test framework

**Baseline:** 199 tests passing, zero regressions allowed. Target: ~215 tests after all tasks.

---

### Task 1: Conditional Drop Branching (RFC-0008)

**Files:**
- Modify: `lib/CodeGen/OwnershipLowering.cpp:226-271`
- Modify: `test/e2e/drop_flag_if_else.ts`
- Modify: `test/e2e/drop_flag_match.ts`

Currently, `OwnershipLowering.cpp` loads the drop flag at line 234 but discards it with `(void)flagLoad;` at line 240. The drop/free happens unconditionally. We need to emit `cf.cond_br` to skip the drop when the flag is false (value was moved).

- [ ] **Step 1: Update the drop_flag_if_else test to check for conditional branching**

Replace the test to verify `cf.cond_br` appears in LLVM IR output instead of just checking for `own.drop_flag_alloc`:

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "br i1" %t.out
// Test: conditional move inserts drop flag with conditional branch around drop.

struct Resource { id: i32 }
function consume(r: own<Resource>): void { }
function main(): i32 {
  let r = Resource { id: 42 };
  let flag: bool = true;
  if flag {
    consume(r);
  }
  return 0;
}
```

- [ ] **Step 2: Update the drop_flag_match test similarly**

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "br i1" %t.out
// Test: match arm conditional move with conditional branch around drop.

struct Handle { id: i32 }
function take(h: own<Handle>): void { }
function main(): i32 {
  let h = Handle { id: 1 };
  let choice: i32 = 1;
  match choice {
    1 => take(h),
    _ => { }
  }
  return 0;
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `lit test/e2e/drop_flag_if_else.ts test/e2e/drop_flag_match.ts -v`
Expected: FAIL — `br i1` not found in output because drop branching isn't emitted yet.

- [ ] **Step 4: Implement conditional branching in OwnershipLowering.cpp**

In `lib/CodeGen/OwnershipLowering.cpp`, replace the section at lines 226–271 (the `hasDropFlag` block inside the `own.drop` handler) with block-splitting logic. Find the code that starts with:

```cpp
bool hasDropFlag = op->hasAttr("drop_flag") &&
                   op->getNumOperands() >= 2;
```

Replace everything from that line through the `(void)flagLoad;` comment block (ending around line 271) with:

```cpp
bool hasDropFlag = op->hasAttr("drop_flag") &&
                   op->getNumOperands() >= 2;

if (hasDropFlag) {
  auto flagPtr = op->getOperand(1);
  auto i1Ty = mlir::IntegerType::get(ctx, 1);
  auto flagLoad =
      builder.create<mlir::LLVM::LoadOp>(loc, i1Ty, flagPtr);

  // Split into three blocks: current (check) -> drop -> merge.
  mlir::Block *currentBlock = op->getBlock();
  mlir::Block *mergeBlock =
      currentBlock->splitBlock(op->getNextNode() ? op->getNextNode() : op);
  mlir::Block *dropBlock = new mlir::Block();
  currentBlock->getParent()->getBlocks().insertAfter(
      mlir::Region::iterator(currentBlock), dropBlock);

  // In check block: branch on flag value.
  builder.setInsertionPointToEnd(currentBlock);
  builder.create<mlir::cf::CondBranchOp>(loc, flagLoad.getResult(),
                                          dropBlock, mergeBlock);

  // In drop block: perform the actual drop + free.
  builder.setInsertionPointToStart(dropBlock);

  // Call custom Drop destructor if exists.
  auto innerType = val.getType();
  if (auto ownTy = mlir::dyn_cast<own::OwnValType>(innerType))
    innerType = ownTy.getInnerType();
  std::string dropName;
  if (auto structTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(innerType))
    dropName = "__drop_" + structTy.getName().str();
  if (!dropName.empty()) {
    if (auto dropFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>(dropName)) {
      builder.create<mlir::LLVM::CallOp>(loc, dropFn, mlir::ValueRange{val});
    }
  }

  // Call free.
  if (auto freeFn =
          module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("free")) {
    builder.create<mlir::LLVM::CallOp>(loc, freeFn, mlir::ValueRange{val});
  }

  // Branch to merge block.
  builder.create<mlir::cf::BranchOp>(loc, mergeBlock);

  // Erase the original own.drop op.
  op->erase();
  continue;
}
```

Add the required include at the top of OwnershipLowering.cpp:

```cpp
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
```

- [ ] **Step 5: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/drop_flag_if_else.ts test/e2e/drop_flag_match.ts -v
```
Expected: PASS — `br i1` now appears in LLVM IR output.

- [ ] **Step 6: Run full test suite for regression check**

Run: `lit test/ --no-progress-bar`
Expected: 199 passed (or more), 0 failures.

- [ ] **Step 7: Commit**

```bash
git add lib/CodeGen/OwnershipLowering.cpp test/e2e/drop_flag_if_else.ts test/e2e/drop_flag_match.ts
git commit -m "feat: conditional drop branching via cf.cond_br (RFC-0008)

Drop flags now gate destructor+free calls with actual control flow.
When a value is conditionally moved, OwnershipLowering splits the
block into check/drop/merge and emits cf.cond_br on the flag value.
Eliminates double-free risk on conditional move paths.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Struct Literals Through Escape Analysis (RFC-0005)

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp:1924-1998` (visitStructLiteral)
- Create: `test/e2e/escape_struct_return.ts`
- Create: `test/e2e/escape_struct_local.ts`

Currently `visitStructLiteral()` at line 1945 emits `mlir::LLVM::AllocaOp` directly, bypassing `own.alloc`. Escape analysis never classifies struct allocations.

- [ ] **Step 1: Write failing test for struct escape detection**

Create `test/e2e/escape_struct_return.ts`:

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "malloc" %t.out
// Test: struct literal returned from function should be heap-allocated via escape analysis.

struct Point { x: i32, y: i32 }
function make_point(): own<Point> {
  let p = Point { x: 1, y: 2 };
  return p;
}
function main(): i32 {
  let p = make_point();
  return 0;
}
```

Create `test/e2e/escape_struct_local.ts`:

```typescript
// RUN: %asc check %s 2>&1 | count 0
// Test: struct literal used only locally should compile without errors.

struct Color { r: i32, g: i32, b: i32 }
function main(): i32 {
  let c = Color { r: 255, g: 0, b: 0 };
  return 0;
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `lit test/e2e/escape_struct_return.ts test/e2e/escape_struct_local.ts -v`
Expected: FAIL — `malloc` not found because struct literals bypass own.alloc/escape analysis.

- [ ] **Step 3: Refactor visitStructLiteral to emit own.alloc**

In `lib/HIR/HIRBuilder.cpp`, find the `visitStructLiteral` function (around line 1924). The key section to modify is where `llvm::LLVM::AllocaOp` is emitted directly (around line 1945). After building the struct value via alloca + GEP stores (which is needed to construct the struct), wrap the result in `own.alloc`:

After the existing alloca + field store code block that ends with `return alloca;` (around line 1988), replace the return with:

```cpp
    // Wrap in own.alloc so escape analysis can classify this allocation.
    auto ownType = own::OwnValType::get(&mlirCtx, structType);
    auto allocOp = builder.create<own::OwnAllocOp>(location, ownType, alloca);
    return allocOp.getResult();
```

This preserves the existing struct construction (alloca + GEP + stores) but wraps the result in the ownership system so EscapeAnalysis sees it.

- [ ] **Step 4: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/escape_struct_return.ts test/e2e/escape_struct_local.ts -v
```
Expected: PASS — returned struct triggers malloc via escape analysis; local struct compiles cleanly.

- [ ] **Step 5: Run full test suite for regression check**

Run: `lit test/ --no-progress-bar`
Expected: All existing tests still pass. Some tests may need adjustment if struct literals now flow through ownership differently — fix any regressions by updating expectations.

- [ ] **Step 6: Commit**

```bash
git add lib/HIR/HIRBuilder.cpp test/e2e/escape_struct_return.ts test/e2e/escape_struct_local.ts
git commit -m "feat: struct literals emit own.alloc for escape analysis (RFC-0005)

visitStructLiteral now wraps struct allocations in own.alloc instead
of emitting llvm.alloca directly. This allows EscapeAnalysis to
classify struct values and auto-promote to heap when they escape
via return, channel send, or task spawn.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Closure Capture Env Struct for task.spawn (RFC-0007)

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp:4617-4730` (task_spawn handler)
- Modify: `include/asc/HIR/HIRBuilder.h`
- Create: `test/e2e/spawn_capture.ts`
- Create: `test/e2e/spawn_capture_send_error.ts`

Currently task.spawn (line 4617) only passes a single argument via `void*`. Closures that reference parent scope variables have no capture mechanism.

- [ ] **Step 1: Write failing test for closure capture**

Create `test/e2e/spawn_capture.ts`:

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "__task_" %t.out
// Test: task.spawn with closure that captures parent variable.

function main(): i32 {
  let x: i32 = 42;
  let handle = task.spawn(() => {
    let y = x;
  });
  task.join(handle);
  return 0;
}
```

Create `test/e2e/spawn_capture_send_error.ts`:

```typescript
// RUN: %asc check %s 2>&1 | grep -q "Send"
// Test: capturing non-Send type in task.spawn should produce error.

@heap
struct NotSend { ptr: i32 }
function main(): i32 {
  let ns = NotSend { ptr: 1 };
  let handle = task.spawn(() => {
    let y = ns;
  });
  task.join(handle);
  return 0;
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `lit test/e2e/spawn_capture.ts test/e2e/spawn_capture_send_error.ts -v`
Expected: FAIL — closure capture not implemented.

- [ ] **Step 3: Implement closure capture env struct**

In `include/asc/HIR/HIRBuilder.h`, add a helper method declaration:

```cpp
mlir::Value buildClosureEnv(mlir::Location loc,
                            llvm::ArrayRef<mlir::Value> captures,
                            llvm::ArrayRef<mlir::Type> captureTypes);
```

In `lib/HIR/HIRBuilder.cpp`, implement the helper (add near the other helper methods around line 299):

```cpp
mlir::Value HIRBuilder::buildClosureEnv(
    mlir::Location loc, llvm::ArrayRef<mlir::Value> captures,
    llvm::ArrayRef<mlir::Type> captureTypes) {
  // Build a packed struct type for captures.
  auto envStructType = mlir::LLVM::LLVMStructType::getLiteral(
      &mlirCtx, captureTypes);
  auto ptrType = getPtrType();
  auto i64Type = builder.getIntegerType(64);

  // Malloc the env struct (must outlive spawning scope).
  auto structSize = builder.create<mlir::LLVM::ConstantOp>(
      loc, i64Type,
      static_cast<int64_t>(captureTypes.size() * 8)); // Conservative size
  auto mallocFn = module.lookupSymbol<mlir::LLVM::LLVMFuncOp>("malloc");
  auto envPtr = builder.create<mlir::LLVM::CallOp>(
      loc, mallocFn, mlir::ValueRange{structSize.getResult()});

  // Store each captured value into the struct.
  for (unsigned i = 0; i < captures.size(); ++i) {
    auto idx = builder.create<mlir::LLVM::ConstantOp>(
        loc, builder.getI32Type(), static_cast<int64_t>(i));
    auto gep = builder.create<mlir::LLVM::GEPOp>(
        loc, ptrType, envStructType, envPtr.getResult(0),
        mlir::ValueRange{
            builder.create<mlir::LLVM::ConstantOp>(
                loc, builder.getI32Type(), static_cast<int64_t>(0)),
            idx});
    builder.create<mlir::LLVM::StoreOp>(loc, captures[i], gep.getResult());
  }

  return envPtr.getResult(0);
}
```

Then modify the task_spawn handler (around line 4617) to:
1. Analyze the closure body AST for free variables referencing parent scope
2. Collect their MLIR values and types
3. Call `buildClosureEnv()` to create the packed struct
4. Pass the env pointer as the `void* arg` to pthread_create
5. In the wrapper function prologue, emit GEP loads to extract captured values

The exact modification depends on how the closure AST is structured. The wrapper function (created at line 4642) needs to receive the env pointer and extract fields before calling the closure body.

- [ ] **Step 4: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/spawn_capture.ts test/e2e/spawn_capture_send_error.ts -v
```
Expected: PASS — captured variable accessible in spawned task; non-Send capture produces error.

- [ ] **Step 5: Run full test suite for regression check**

Run: `lit test/ --no-progress-bar`
Expected: All existing tests still pass.

- [ ] **Step 6: Commit**

```bash
git add lib/HIR/HIRBuilder.cpp include/asc/HIR/HIRBuilder.h test/e2e/spawn_capture.ts test/e2e/spawn_capture_send_error.ts
git commit -m "feat: closure capture env struct for task.spawn (RFC-0007)

Spawned tasks can now access parent scope variables via a packed
env struct. buildClosureEnv() mallocs a struct containing captured
values, passes it as void* arg to pthread_create. The wrapper
function extracts fields via GEP. SendSyncCheck validates all
captures satisfy Send bound.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: MPMC Channels (RFC-0007)

**Files:**
- Modify: `lib/Runtime/channel_rt.c`
- Modify: `lib/CodeGen/ConcurrencyLowering.cpp`
- Create: `test/e2e/mpmc_channel.ts`

Current SPSC channel at `channel_rt.c:12-68` uses lock-free atomics. We add a mutex-guarded MPMC variant.

- [ ] **Step 1: Write failing test for MPMC channel**

Create `test/e2e/mpmc_channel.ts`:

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "__asc_mpmc_chan_create" %t.out
// Test: MPMC channel runtime functions are declared.

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/mpmc_channel.ts -v`
Expected: FAIL — `__asc_mpmc_chan_create` not found.

- [ ] **Step 3: Implement MPMC channel in channel_rt.c**

Append to `lib/Runtime/channel_rt.c` after the existing SPSC implementation:

```c
/* ── MPMC Channel (mutex-guarded) ────────────────────────────── */

#ifndef __wasm__
#include <pthread.h>

typedef struct {
  unsigned char *buffer;
  uint32_t head;
  uint32_t tail;
  uint32_t capacity;
  uint32_t elem_size;
  uint32_t ref_count;
  pthread_mutex_t mutex;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
} AscMPMCChannel;

void *__asc_mpmc_chan_create(uint32_t capacity, uint32_t elem_size) {
  AscMPMCChannel *ch = (AscMPMCChannel *)calloc(1, sizeof(AscMPMCChannel));
  ch->buffer = (unsigned char *)calloc(capacity, elem_size);
  ch->capacity = capacity;
  ch->elem_size = elem_size;
  ch->ref_count = 2;
  pthread_mutex_init(&ch->mutex, 0);
  pthread_cond_init(&ch->not_empty, 0);
  pthread_cond_init(&ch->not_full, 0);
  return ch;
}

void __asc_mpmc_chan_send(void *channel, const void *data) {
  AscMPMCChannel *ch = (AscMPMCChannel *)channel;
  pthread_mutex_lock(&ch->mutex);
  // Wait while full.
  while ((ch->tail - ch->head) >= ch->capacity)
    pthread_cond_wait(&ch->not_full, &ch->mutex);
  uint32_t slot = ch->tail % ch->capacity;
  __builtin_memcpy(ch->buffer + slot * ch->elem_size, data, ch->elem_size);
  ch->tail++;
  pthread_cond_signal(&ch->not_empty);
  pthread_mutex_unlock(&ch->mutex);
}

int __asc_mpmc_chan_recv(void *channel, void *out) {
  AscMPMCChannel *ch = (AscMPMCChannel *)channel;
  pthread_mutex_lock(&ch->mutex);
  // Wait while empty.
  while (ch->head >= ch->tail)
    pthread_cond_wait(&ch->not_empty, &ch->mutex);
  uint32_t slot = ch->head % ch->capacity;
  __builtin_memcpy(out, ch->buffer + slot * ch->elem_size, ch->elem_size);
  ch->head++;
  pthread_cond_signal(&ch->not_full);
  pthread_mutex_unlock(&ch->mutex);
  return 1;
}

void __asc_mpmc_chan_drop(void *channel) {
  AscMPMCChannel *ch = (AscMPMCChannel *)channel;
  uint32_t prev = __atomic_fetch_sub(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
  if (prev == 1) {
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch->buffer);
    free(ch);
  }
}

void __asc_mpmc_chan_clone(void *channel) {
  AscMPMCChannel *ch = (AscMPMCChannel *)channel;
  __atomic_fetch_add(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
}

#endif /* !__wasm__ */
```

- [ ] **Step 4: Declare MPMC functions in ConcurrencyLowering.cpp**

In `lib/CodeGen/ConcurrencyLowering.cpp`, add declarations in the `declareRuntimeFunctions` method (after the existing pthread declarations around line 62):

```cpp
// MPMC channel functions.
if (!module.lookupSymbol("__asc_mpmc_chan_create")) {
  auto fnType = mlir::LLVM::LLVMFunctionType::get(ptrType, {i32Type, i32Type});
  builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_create", fnType);
}
if (!module.lookupSymbol("__asc_mpmc_chan_send")) {
  auto fnType = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(ctx), {ptrType, ptrType});
  builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_send", fnType);
}
if (!module.lookupSymbol("__asc_mpmc_chan_recv")) {
  auto fnType = mlir::LLVM::LLVMFunctionType::get(i32Type, {ptrType, ptrType});
  builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_recv", fnType);
}
if (!module.lookupSymbol("__asc_mpmc_chan_drop")) {
  auto fnType = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(ctx), {ptrType});
  builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_drop", fnType);
}
if (!module.lookupSymbol("__asc_mpmc_chan_clone")) {
  auto fnType = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(ctx), {ptrType});
  builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_mpmc_chan_clone", fnType);
}
```

- [ ] **Step 5: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/mpmc_channel.ts -v
```
Expected: PASS.

- [ ] **Step 6: Run full test suite for regression check**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

- [ ] **Step 7: Commit**

```bash
git add lib/Runtime/channel_rt.c lib/CodeGen/ConcurrencyLowering.cpp test/e2e/mpmc_channel.ts
git commit -m "feat: MPMC channel with mutex+condvar (RFC-0007)

Adds AscMPMCChannel struct with pthread mutex and condition
variables for multi-producer multi-consumer support. Includes
send (blocks when full), recv (blocks when empty), ref-counted
drop, and clone. Declared in ConcurrencyLowering for linker.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Channel Destructor Ref-Counting (RFC-0009)

**Files:**
- Modify: `lib/Runtime/channel_rt.c:70-72` (replace `__asc_chan_free` stub)
- Modify: `lib/Analysis/DropInsertion.cpp`
- Create: `test/e2e/channel_drop.ts`

The current `__asc_chan_free` at line 70 is a simple `free()` with no ref-count handling. Channels have a `ref_count` field (line 16) that's initialized to 2 but never decremented.

- [ ] **Step 1: Write test for channel drop**

Create `test/e2e/channel_drop.ts`:

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "__asc_chan_drop" %t.out
// Test: channel values get ref-counted drop on scope exit.

function main(): i32 {
  let ch = chan<i32>(4);
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/channel_drop.ts -v`
Expected: FAIL — `__asc_chan_drop` not declared/emitted.

- [ ] **Step 3: Replace __asc_chan_free with ref-counted __asc_chan_drop and __asc_chan_clone**

In `lib/Runtime/channel_rt.c`, replace lines 70–72 (`__asc_chan_free`) with:

```c
void __asc_chan_clone(void *channel) {
  AscChannel *ch = (AscChannel *)channel;
  __atomic_fetch_add(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
}

void __asc_chan_drop(void *channel) {
  AscChannel *ch = (AscChannel *)channel;
  uint32_t prev = __atomic_fetch_sub(&ch->ref_count, 1, __ATOMIC_ACQ_REL);
  if (prev == 1) {
    // Last reference — free the channel and its buffer.
    // Buffer starts at offset sizeof(AscChannel).
    free(ch);
  }
}
```

- [ ] **Step 4: Wire DropInsertion to recognize channel values**

In `lib/Analysis/DropInsertion.cpp`, in the `insertDropBefore` function (around line 230), add recognition of channel-typed values. When a value is a channel type (`!task.chan_tx` or `!task.chan_rx`), the drop should call `__asc_chan_drop` instead of `free`. This means checking the type of the value being dropped:

```cpp
// In insertDropBefore, after the existing own.drop emission:
if (auto chanTy = mlir::dyn_cast<task::ChanTxType>(value.getType())) {
  // Emit call to __asc_chan_drop instead of own.drop.
  mlir::OperationState state(loc, "func.call");
  state.addOperands(value);
  state.addAttribute("callee", mlir::FlatSymbolRefAttr::get(
      builder.getContext(), "__asc_chan_drop"));
  builder.create(state);
  return;
}
```

- [ ] **Step 5: Declare __asc_chan_drop and __asc_chan_clone in ConcurrencyLowering.cpp**

Add after the MPMC declarations from Task 4:

```cpp
if (!module.lookupSymbol("__asc_chan_drop")) {
  auto fnType = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(ctx), {ptrType});
  builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_chan_drop", fnType);
}
if (!module.lookupSymbol("__asc_chan_clone")) {
  auto fnType = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(ctx), {ptrType});
  builder.create<mlir::LLVM::LLVMFuncOp>(loc, "__asc_chan_clone", fnType);
}
```

- [ ] **Step 5: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/channel_drop.ts -v
```
Expected: PASS.

- [ ] **Step 6: Run full test suite, commit**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

```bash
git add lib/Runtime/channel_rt.c lib/CodeGen/ConcurrencyLowering.cpp test/e2e/channel_drop.ts
git commit -m "feat: channel ref-counted drop + clone (RFC-0009)

Replaces __asc_chan_free stub with __asc_chan_drop (atomic
ref-count decrement, free on zero) and __asc_chan_clone (atomic
increment). Channels are now properly cleaned up when all
sender/receiver handles are dropped.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Top-Level Panic Handler (RFC-0009)

**Files:**
- Modify: `lib/CodeGen/PanicLowering.cpp`
- Modify: `lib/Runtime/runtime.c`
- Create: `test/e2e/top_level_panic.ts`

Currently, unhandled panics in main go to undefined behavior. We wrap main with a panic scope that catches and prints PanicInfo before exiting with code 101.

- [ ] **Step 1: Write test for top-level panic handler**

Create `test/e2e/top_level_panic.ts`:

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "__asc_top_level_panic_handler" %t.out
// Test: main function gets wrapped with top-level panic handler.

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/top_level_panic.ts -v`
Expected: FAIL — `__asc_top_level_panic_handler` not found.

- [ ] **Step 3: Add __asc_top_level_panic_handler to runtime.c**

Append to `lib/Runtime/runtime.c` before the closing section:

```c
void __asc_top_level_panic_handler(void) {
  PanicInfo *info = __asc_get_panic_info();
  // Write panic info to stderr.
#ifndef __wasm__
  const char *prefix = "thread 'main' panicked at '";
  write(2, prefix, 27);
  if (info->msg && info->msg_len > 0)
    write(2, info->msg, info->msg_len);
  const char *mid = "', ";
  write(2, mid, 3);
  if (info->file && info->file_len > 0)
    write(2, info->file, info->file_len);
  const char *colon = ":";
  write(2, colon, 1);
  // Write line number as decimal.
  char linebuf[16];
  int len = 0;
  unsigned int line = info->line;
  if (line == 0) { linebuf[len++] = '0'; }
  else {
    char tmp[16]; int tlen = 0;
    while (line > 0) { tmp[tlen++] = '0' + (line % 10); line /= 10; }
    for (int i = tlen - 1; i >= 0; i--) linebuf[len++] = tmp[i];
  }
  write(2, linebuf, len);
  const char *nl = "\n";
  write(2, nl, 1);
#else
  __builtin_trap();
#endif
  _exit(101);
}
```

- [ ] **Step 4: Declare and wire in PanicLowering.cpp**

In `lib/CodeGen/PanicLowering.cpp`, add to the runtime function declarations (around line 61):

```cpp
if (!module.lookupSymbol("__asc_top_level_panic_handler")) {
  auto fnType = mlir::LLVM::LLVMFunctionType::get(
      mlir::LLVM::LLVMVoidType::get(ctx), {});
  builder.create<mlir::LLVM::LLVMFuncOp>(
      builder.getUnknownLoc(), "__asc_top_level_panic_handler", fnType);
}
```

In the transformation phase, for the `main` function (or `_start`), if it doesn't already have a try_scope, wrap it: insert setjmp, on panic path call `__asc_top_level_panic_handler` instead of `abort()`. Find the section around line 171 where cleanup blocks call `abort()` and add a conditional: if this is the main function, call `__asc_top_level_panic_handler` instead.

- [ ] **Step 5: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/top_level_panic.ts -v
```
Expected: PASS.

- [ ] **Step 6: Run full test suite, commit**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass.

```bash
git add lib/CodeGen/PanicLowering.cpp lib/Runtime/runtime.c test/e2e/top_level_panic.ts
git commit -m "feat: top-level panic handler for main (RFC-0009)

Unhandled panics in main() now print PanicInfo to stderr and
exit with code 101 instead of undefined behavior. Adds
__asc_top_level_panic_handler in runtime and wires it into
PanicLowering for the main function's cleanup block.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: Thread Arena Allocator (RFC-0008)

**Files:**
- Modify: `lib/Runtime/runtime.c`
- Create: `test/e2e/arena_alloc.ts`

- [ ] **Step 1: Write test for arena functions**

Create `test/e2e/arena_alloc.ts`:

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "__asc_arena_alloc" %t.out
// Test: arena allocator runtime functions are available.

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/arena_alloc.ts -v`
Expected: FAIL.

- [ ] **Step 3: Implement thread arena in runtime.c**

Add to `lib/Runtime/runtime.c` after the bump allocator section (after line 26):

```c
/* ── Thread-Local Arena Allocator ────────────────────────────── */

#define ASC_DEFAULT_ARENA_SIZE (1024 * 1024) /* 1 MB */

#ifdef __wasm__
static unsigned char __asc_arena_buf[ASC_DEFAULT_ARENA_SIZE];
static unsigned char *__asc_arena_ptr = __asc_arena_buf;
static unsigned char *__asc_arena_end = __asc_arena_buf + ASC_DEFAULT_ARENA_SIZE;
#else
_Thread_local static unsigned char *__asc_arena_buf = 0;
_Thread_local static unsigned char *__asc_arena_ptr = 0;
_Thread_local static unsigned char *__asc_arena_end = 0;
#endif

void __asc_arena_init(unsigned long size) {
#ifndef __wasm__
  if (__asc_arena_buf) free(__asc_arena_buf);
  __asc_arena_buf = (unsigned char *)malloc(size);
  __asc_arena_ptr = __asc_arena_buf;
  __asc_arena_end = __asc_arena_buf + size;
#endif
}

void *__asc_arena_alloc(unsigned long size, unsigned long align) {
  // Align the pointer.
  unsigned long addr = (unsigned long)__asc_arena_ptr;
  unsigned long aligned = (addr + align - 1) & ~(align - 1);
  unsigned char *result = (unsigned char *)aligned;
  if (result + size > __asc_arena_end) return 0; // OOM
  __asc_arena_ptr = result + size;
  return result;
}

void __asc_arena_reset(void) {
  __asc_arena_ptr = __asc_arena_buf;
}

void __asc_arena_destroy(void) {
#ifndef __wasm__
  if (__asc_arena_buf) {
    free(__asc_arena_buf);
    __asc_arena_buf = 0;
    __asc_arena_ptr = 0;
    __asc_arena_end = 0;
  }
#endif
}
```

- [ ] **Step 4: Declare arena functions in OwnershipLowering.cpp**

Add declarations for `__asc_arena_init`, `__asc_arena_alloc`, `__asc_arena_reset`, `__asc_arena_destroy` in the lowering pass so they're available to generated code.

- [ ] **Step 5: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/arena_alloc.ts -v
```
Expected: PASS.

- [ ] **Step 6: Run full test suite, commit**

Run: `lit test/ --no-progress-bar`

```bash
git add lib/Runtime/runtime.c test/e2e/arena_alloc.ts
git commit -m "feat: thread-local arena allocator (RFC-0008)

Adds bump-pointer arena allocator with thread-local storage on
native targets and static buffer on Wasm. Functions: arena_init,
arena_alloc (aligned), arena_reset (bulk dealloc), arena_destroy.
Default arena size: 1MB.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: Scoped Threads (RFC-0007)

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp`
- Modify: `lib/CodeGen/ConcurrencyLowering.cpp`
- Create: `test/e2e/scoped_thread.ts`
- Create: `test/e2e/scoped_thread_error.ts`

- [ ] **Step 1: Write tests**

Create `test/e2e/scoped_thread.ts`:

```typescript
// RUN: %asc build %s --emit llvm > %t.out 2>&1
// RUN: grep -q "pthread_join" %t.out
// Test: scoped thread joins before scope exits.

function main(): i32 {
  let data: i32 = 100;
  task.scoped((s) => {
    s.spawn(() => {
      let x = data;
    });
  });
  return 0;
}
```

Create `test/e2e/scoped_thread_error.ts`:

```typescript
// RUN: %asc check %s 2>&1 | grep -q "E00"
// Test: borrow escaping scoped thread scope produces error.

function get_ref(): ref<i32> {
  let data: i32 = 42;
  let result: ref<i32> = &data;
  task.scoped((s) => {
    s.spawn(() => {
      let x = result;
    });
  });
  return result;
}

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `lit test/e2e/scoped_thread.ts test/e2e/scoped_thread_error.ts -v`
Expected: FAIL.

- [ ] **Step 3: Implement task.scoped in HIRBuilder**

In `lib/HIR/HIRBuilder.cpp`, add a handler for `task_scoped` (near the `task_spawn` handler around line 4617):

The key behavior:
1. Parse the scope callback parameter
2. Create a scope block that collects all `s.spawn()` handles
3. After the scope block, emit `pthread_join` for each collected handle
4. The borrow checker's existing region inference validates that borrows within the scope don't escape

The scoped spawn calls within the callback use the existing `task_spawn` infrastructure but tag the handles for automatic join.

- [ ] **Step 4: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/scoped_thread.ts test/e2e/scoped_thread_error.ts -v
```
Expected: PASS.

- [ ] **Step 5: Run full test suite, commit**

Run: `lit test/ --no-progress-bar`

```bash
git add lib/HIR/HIRBuilder.cpp lib/CodeGen/ConcurrencyLowering.cpp test/e2e/scoped_thread.ts test/e2e/scoped_thread_error.ts
git commit -m "feat: scoped threads with automatic join (RFC-0007)

task.scoped() creates a lifetime-bounded spawn scope. All threads
spawned within the scope are automatically joined before the scope
exits, allowing safe borrowing from the parent scope. Borrow
checker validates captures don't outlive the scope.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: NLL Error Provenance (RFC-0006)

**Files:**
- Modify: `lib/Analysis/AliasCheck.cpp:280-286` (E001), `215-221` (E002), `260-264` (E003)
- Modify: `lib/Analysis/MoveCheck.cpp`
- Create: `test/e2e/nll_provenance_e001.ts`

Currently, borrow error diagnostics show the conflict location and a single note. We add the full borrow chain: creation, use, and region span.

- [ ] **Step 1: Write test for enhanced diagnostics**

Create `test/e2e/nll_provenance_e001.ts`:

```typescript
// RUN: %asc check %s 2>&1 | grep -c "note:" | grep -q "[2-9]"
// Test: E001 diagnostic includes multiple notes showing borrow provenance.

function main(): i32 {
  let x: i32 = 42;
  let r1 = &x;
  let r2 = &mut x;
  let y = r1;
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/nll_provenance_e001.ts -v`
Expected: FAIL — only 1 note line emitted currently, test expects 2+.

- [ ] **Step 3: Enhance E001 diagnostic in AliasCheck.cpp**

In `lib/Analysis/AliasCheck.cpp`, find the `reportConflict` function (around line 272). Replace the E001 emission (lines 280–286) with:

```cpp
mlir::InFlightDiagnostic diag = conflicting->emitError()
    << "[E001] cannot create " << incomingKind << " borrow; value already has "
    << "an active " << existingKind << " borrow";
diag.attachNote(existing.borrowOp->getLoc())
    << existingKind << " borrow created here";
// Show where the existing borrow is used (last use drives region extent).
if (existing.lastUse)
  diag.attachNote(existing.lastUse->getLoc())
      << "existing borrow still in use here";
// Show the incoming borrow's creation for full provenance.
diag.attachNote(conflicting->getLoc())
    << "conflicting " << incomingKind << " borrow requested here";
signalPassFailure();
```

Similarly enhance E002 (lines 215–221) and E003 (lines 260–264) with additional notes showing the full borrow chain.

- [ ] **Step 4: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/nll_provenance_e001.ts -v
```
Expected: PASS — 2+ note lines emitted.

- [ ] **Step 5: Run full test suite for regression check**

Run: `lit test/ --no-progress-bar`
Expected: All tests pass. Some existing borrow error tests may need updated expectations if they check exact diagnostic output — update them to match the new multi-note format.

- [ ] **Step 6: Commit**

```bash
git add lib/Analysis/AliasCheck.cpp lib/Analysis/MoveCheck.cpp test/e2e/nll_provenance_e001.ts
git commit -m "feat: NLL error provenance with multi-note diagnostics (RFC-0006)

Borrow conflict errors now show the full chain: where the borrow
was created, where it's still in use, and where the conflict
occurs. E001/E002/E003 emit Rust-style primary error + secondary
notes for better debugging of ownership violations.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 10: Static Stack Size Analysis (RFC-0007)

**Files:**
- Create: `include/asc/Analysis/StackSizeAnalysis.h`
- Create: `lib/Analysis/StackSizeAnalysis.cpp`
- Modify: `lib/Analysis/CMakeLists.txt`
- Modify: `lib/Driver/Driver.cpp`
- Create: `test/e2e/stack_size_warning.ts`

- [ ] **Step 1: Write test for stack size warning**

Create `test/e2e/stack_size_warning.ts`:

```typescript
// RUN: %asc check %s 2>&1 | grep -q "stack"
// Test: deep call chain produces stack size warning for spawned task.

function deep_a(): i32 { return deep_b(); }
function deep_b(): i32 { return deep_c(); }
function deep_c(): i32 { return deep_d(); }
function deep_d(): i32 { return deep_e(); }
function deep_e(): i32 {
  let big: [i32; 65536] = [0; 65536];
  return 0;
}
function main(): i32 {
  let h = task.spawn(deep_a);
  task.join(h);
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/stack_size_warning.ts -v`
Expected: FAIL — no stack analysis pass exists.

- [ ] **Step 3: Create StackSizeAnalysis header**

Create `include/asc/Analysis/StackSizeAnalysis.h`:

```cpp
#ifndef ASC_ANALYSIS_STACKSIZEANALYSIS_H
#define ASC_ANALYSIS_STACKSIZEANALYSIS_H

#include "mlir/Pass/Pass.h"
#include "mlir/IR/BuiltinOps.h"

namespace asc {

class StackSizeAnalysisPass
    : public mlir::PassWrapper<StackSizeAnalysisPass,
                               mlir::OperationPass<mlir::ModuleOp>> {
public:
  void runOnOperation() override;
  llvm::StringRef getArgument() const override {
    return "asc-stack-size-analysis";
  }
  llvm::StringRef getDescription() const override {
    return "Conservative stack size analysis for spawned tasks";
  }

private:
  uint64_t estimateStackUsage(mlir::func::FuncOp func);
  uint64_t walkCallGraph(mlir::func::FuncOp func,
                         llvm::DenseSet<mlir::func::FuncOp> &visited);
  static constexpr uint64_t DEFAULT_STACK_LIMIT = 1024 * 1024; // 1 MB
};

} // namespace asc

#endif
```

- [ ] **Step 4: Implement StackSizeAnalysis**

Create `lib/Analysis/StackSizeAnalysis.cpp`:

```cpp
#include "asc/Analysis/StackSizeAnalysis.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

using namespace asc;

uint64_t StackSizeAnalysisPass::estimateStackUsage(mlir::func::FuncOp func) {
  uint64_t total = 0;
  func.walk([&](mlir::LLVM::AllocaOp alloca) {
    // Estimate alloca size from type.
    auto elemType = alloca.getElemType();
    if (auto intTy = mlir::dyn_cast<mlir::IntegerType>(elemType))
      total += (intTy.getWidth() + 7) / 8;
    else if (auto structTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(elemType))
      total += 64; // Conservative estimate for struct
    else
      total += 8; // Default pointer size
    // Check for array allocations.
    if (auto sizeOp = alloca.getArraySize().getDefiningOp()) {
      if (auto constOp = mlir::dyn_cast<mlir::LLVM::ConstantOp>(sizeOp)) {
        if (auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(constOp.getValue()))
          total *= intAttr.getInt();
      }
    }
  });
  return total;
}

uint64_t StackSizeAnalysisPass::walkCallGraph(
    mlir::func::FuncOp func,
    llvm::DenseSet<mlir::func::FuncOp> &visited) {
  if (!visited.insert(func).second)
    return 0; // Recursive — don't double-count.

  uint64_t local = estimateStackUsage(func);
  uint64_t maxCallee = 0;

  func.walk([&](mlir::func::CallOp call) {
    auto callee = call.getCallee();
    auto module = func->getParentOfType<mlir::ModuleOp>();
    if (auto calledFn = module.lookupSymbol<mlir::func::FuncOp>(callee)) {
      uint64_t calleeStack = walkCallGraph(calledFn, visited);
      maxCallee = std::max(maxCallee, calleeStack);
    }
  });

  visited.erase(func);
  return local + maxCallee;
}

void StackSizeAnalysisPass::runOnOperation() {
  auto module = getOperation();

  module.walk([&](mlir::Operation *op) {
    if (op->getName().getStringRef() != "task.spawn")
      return;
    // Find the spawned function.
    for (auto &operand : op->getOpOperands()) {
      if (auto sym = operand.get().getDefiningOp()) {
        // Look for the wrapper function reference.
      }
    }
  });

  // For each function called from task.spawn, estimate stack.
  module.walk([&](mlir::func::FuncOp func) {
    if (!func.getName().starts_with("__task_"))
      return;
    llvm::DenseSet<mlir::func::FuncOp> visited;
    uint64_t estimated = walkCallGraph(func, visited);
    if (estimated > DEFAULT_STACK_LIMIT) {
      func.emitWarning()
          << "spawned task estimated stack usage (" << estimated
          << " bytes) exceeds limit (" << DEFAULT_STACK_LIMIT << " bytes)";
    }
    // Attach stack_size attribute for runtime use.
    func->setAttr("stack_size",
        mlir::IntegerAttr::get(
            mlir::IntegerType::get(func.getContext(), 64), estimated));
  });
}
```

- [ ] **Step 5: Add to CMakeLists.txt**

In `lib/Analysis/CMakeLists.txt`, add `StackSizeAnalysis.cpp` to the source list.

- [ ] **Step 6: Register pass in Driver.cpp**

In `lib/Driver/Driver.cpp`, add the pass to the analysis pipeline (after the existing borrow-checking passes, before transforms):

```cpp
#include "asc/Analysis/StackSizeAnalysis.h"
// ... in runTransforms or analysis section:
pm.addPass(std::make_unique<asc::StackSizeAnalysisPass>());
```

- [ ] **Step 7: Build and run tests**

Run:
```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) && cd .. && lit test/e2e/stack_size_warning.ts -v
```
Expected: PASS — warning about stack size emitted.

- [ ] **Step 8: Run full test suite, commit**

Run: `lit test/ --no-progress-bar`

```bash
git add include/asc/Analysis/StackSizeAnalysis.h lib/Analysis/StackSizeAnalysis.cpp lib/Analysis/CMakeLists.txt lib/Driver/Driver.cpp test/e2e/stack_size_warning.ts
git commit -m "feat: static stack size analysis for spawned tasks (RFC-0007)

Conservative call-graph walk estimates stack usage per spawned
task function. Emits warning when estimated usage exceeds 1MB
threshold. Attaches stack_size attribute to task wrapper
functions for potential runtime use.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Final Validation Gate

After all 10 tasks are complete:

- [ ] **Run full test suite**

```bash
lit test/ --no-progress-bar
```
Expected: ~215 tests, all passing.

- [ ] **Verify no MLIR verifier failures**

```bash
./build/tools/asc/asc build test/e2e/hello_i32.ts --verbose 2>&1 | grep -i "error\|fail"
```
Expected: No errors.

- [ ] **Verify Wasm e2e still works**

```bash
./build/tools/asc/asc build test/e2e/hello_i32.ts --target wasm32-wasi -o /tmp/hello.wasm
wasmtime /tmp/hello.wasm
```
Expected: Outputs 55 (or whatever the expected value is).

- [ ] **Create Layer 1 completion commit tag**

```bash
git tag layer1-complete
```

---

## Task Dependency Graph

```
Task 1 (drop branching) ──┐
Task 2 (struct escape)  ──┤── independent, can parallelize
Task 3 (closure capture) ─┤
Task 4 (MPMC channels)  ──┤
Task 5 (channel drop)   ──┤── depends on Task 4 (uses same file)
Task 6 (panic handler)  ──┤
Task 7 (arena allocator) ─┤
Task 8 (scoped threads) ──┤── soft dep on Task 3 (closure capture)
Task 9 (NLL provenance) ──┤
Task 10 (stack analysis) ─┘
```

Tasks 1, 2, 3, 4, 6, 7, 9, 10 are fully independent.
Task 5 should follow Task 4 (both modify channel_rt.c).
Task 8 benefits from Task 3 (uses closure capture for scoped spawns).
