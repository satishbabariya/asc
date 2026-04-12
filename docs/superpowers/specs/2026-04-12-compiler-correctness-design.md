# Compiler Correctness: Ownership Model Hardening

**Date:** 2026-04-12
**RFCs:** 0005, 0006, 0008, 0009
**Goal:** Close the 6 most impactful soundness gaps in the ownership model and borrow checker.

## Context

The compiler is at ~58% weighted RFC coverage with 188/188 tests passing. The ownership pipeline works end-to-end but has known soundness holes: `!own.val` values can be consumed multiple times without error, conditional moves only warn (no runtime drop flags), heap/stack placement is manual-only, borrow region tracking doesn't feed into alias checking, and panic catch blocks can't access panic metadata.

This spec covers 6 items organized into 2 phases. Wasm EH (setjmp→Wasm exception handling) is deferred — the current approach is correct, just not idiomatic.

## Phase 1 — Parallel (4 independent items)

### 1. Linearity Verifier

**Problem:** No enforcement that every `!own.val<T>` has exactly one consuming use. A value can be consumed twice (double-free) or never consumed (leak) without any diagnostic.

**Files:**
- New: `lib/Analysis/LinearityCheck.cpp` (~200 LOC)
- New: `include/asc/Analysis/LinearityCheck.h` (~30 LOC)
- Modify: `lib/Driver/Driver.cpp` — add pass to `runAnalysis()` after MoveCheck

**Algorithm:**
1. Walk each function. Collect all SSA values of type `!own.val<T>`.
2. For each value, identify consuming ops: `own.move`, `own.drop`, `own.store`, `func.call` (owned args), `chan.send`, `func.return`.
3. Count consuming uses per value per control-flow path.
4. Emit errors:
   - **E005 "value never consumed"** — 0 consuming uses on any path (resource leak)
   - **E006 "value consumed multiple times"** — 2+ consuming uses on any path (double-free)
5. For conditional branches (if/else, match): each arm must independently satisfy linearity. Values in `MaybeMoved` state (from MoveCheck) are deferred to drop flags (item 2).

**Integration:** Runs after MoveCheck, before DropInsertion. MoveCheck detects use-after-move (E004); LinearityCheck detects the dual: unconsumed and double-consume.

**Tests:**
- `test/e2e/linearity_double_consume.ts` — two moves of same value → E006
- `test/e2e/linearity_leak.ts` — owned value never dropped/moved → E005
- `test/e2e/linearity_conditional_ok.ts` — each branch consumes once → passes
- `test/e2e/linearity_copy_ok.ts` — `@copy` types exempt from linearity → passes

### 2. Drop Flags for Conditional Moves

**Problem:** When a value is moved in one branch but not another, MoveCheck emits a warning (MaybeMoved state). At runtime, the drop point always runs, causing double-free if the value was already moved. RFC-0008 specifies runtime drop flags.

**Files:**
- Modify: `include/asc/HIR/OwnOps.h` — add `OwnDropFlagAllocOp`, `OwnDropFlagSetOp`, `OwnDropFlagCheckOp` (~30 LOC)
- Modify: `lib/Analysis/DropInsertion.cpp` — emit drop flag ops for MaybeMoved values (~80 LOC)
- Modify: `lib/CodeGen/OwnershipLowering.cpp` — lower drop flag ops to LLVM alloca/store/load (~50 LOC)
- Modify: `lib/HIR/OwnDialect.cpp` — register new ops

**New Ops:**
```
own.drop_flag_alloc : () -> i1*        // alloca i1, init to true
own.drop_flag_set   : (i1*, i1) -> ()  // store false after move
own.drop_flag_check : (i1*) -> i1      // load flag value
```

**Algorithm:**
1. During DropInsertion, when inserting `own.drop` for a value that MoveCheck marked as `MaybeMoved`:
   a. At function entry: emit `own.drop_flag_alloc` → flag ptr
   b. At each `own.move` site for this value: emit `own.drop_flag_set(flag, false)`
   c. At drop point: emit `flag = own.drop_flag_check(ptr); if (flag) { own.drop(val) }`
2. OwnershipLowering lowers these to `llvm.alloca i1`, `llvm.store`, `llvm.load` + `llvm.cond_br`.

**Integration:** DropInsertion queries MoveCheck results (passed via analysis manager or function attribute). MoveCheck warning for conditional moves becomes an info-note ("drop flag inserted").

**Tests:**
- `test/e2e/drop_flag_if_else.ts` — value moved in if but not else → no double-free
- `test/e2e/drop_flag_match.ts` — value moved in one match arm → correct cleanup
- `test/e2e/drop_flag_loop.ts` — value moved on break → drop runs only if not moved

### 3. Escape Analysis

**Problem:** Stack vs heap placement is entirely manual (`@heap` attribute). Values that escape their scope (returned, stored globally, captured by task.spawn) are silently stack-allocated, causing dangling pointers.

**Files:**
- New: `lib/Analysis/EscapeAnalysis.cpp` (~150 LOC)
- New: `include/asc/Analysis/EscapeAnalysis.h` (~30 LOC)
- Modify: `lib/CodeGen/OwnershipLowering.cpp` — consult escape result (~20 LOC)
- Modify: `lib/Driver/Driver.cpp` — add pass before codegen

**Algorithm:**
1. For each `own.alloc` result, walk SSA use-def chains.
2. Classify escape status:
   - **StackSafe**: All uses are local borrows, moves within function, or drops
   - **MustHeap**: Value is operand of `func.return`, stored to non-local memory, passed to `task.spawn`, or used as `chan.send` operand
   - **Unknown**: Can't determine (conservative → heap)
3. Result: `EscapeAnalysisResult` mapping `own.alloc` ops → `{StackSafe, MustHeap, Unknown}`

**Integration with OwnershipLowering:**
- `MustHeap` → `malloc` even without `@heap` attribute
- `StackSafe` + has `@heap` → emit warning W004 "unnecessary @heap annotation"
- `Unknown` → heap (conservative)

**Tests:**
- `test/e2e/escape_return.ts` — returned value auto-promoted to heap
- `test/e2e/escape_local_ok.ts` — purely local value stays on stack
- `test/e2e/escape_task_spawn.ts` — value passed to task.spawn → heap

### 4. PanicInfo Enhancement

**Problem:** The panic infrastructure exists (`__asc_panic` takes file/line/col, TLS `__asc_in_unwind` detects double-panic) but catch blocks can't access panic metadata, and double-panic just traps silently.

**Current state (runtime.c):**
- `_Thread_local static int __asc_in_unwind` (line 50)
- `_Thread_local static jmp_buf *__asc_panic_jmpbuf` (line 51)
- `__asc_panic(msg, msg_len, file, file_len, line, col)` (line 69)
- Double-panic: `__builtin_trap()` with no message (line 72-75)

**Files:**
- Modify: `lib/Runtime/runtime.c` (~50 LOC)
- Modify: `lib/CodeGen/PanicLowering.cpp` (~20 LOC)

**Changes:**

**runtime.c:**
```c
typedef struct {
    const char *msg;
    uint32_t msg_len;
    const char *file;
    uint32_t file_len;
    uint32_t line;
    uint32_t col;
} PanicInfo;

_Thread_local static PanicInfo __asc_panic_info;
```

- `__asc_panic`: Store all args into `__asc_panic_info` before longjmp
- `__asc_get_panic_info()`: Returns `&__asc_panic_info` (for catch block access)
- Double-panic: Write "thread panicked while panicking" + both locations to stderr before trap

**PanicLowering.cpp:**
- In catch/cleanup blocks: declare and call `__asc_get_panic_info` so user code can access it
- No signature change to `__asc_panic` (already takes all fields)

**Tests:**
- `test/e2e/panic_info_access.ts` — catch block reads panic message
- `test/e2e/double_panic_message.ts` — nested panic prints diagnostic before abort

## Phase 2 — Sequential (dependency chain)

### 5. Region Tokens on Borrow Ops

**Problem:** `BorrowRefOp` and `BorrowMutOp` carry no region information. RegionInference computes regions but doesn't annotate the IR. AliasCheck doesn't use region results (despite a comment claiming it does).

**Files:**
- Modify: `include/asc/HIR/OwnOps.h` — add `regionId` attribute to BorrowRefOp/BorrowMutOp (~20 LOC)
- Modify: `lib/Analysis/RegionInference.cpp` — set attribute after region assignment (~30 LOC)
- Modify: `include/asc/Analysis/RegionInference.h` — add `getRegionId(Value)` query (~10 LOC)

**Changes:**

**OwnOps.h:**
Add `OptionalAttr<I32Attr>:$regionId` to both BorrowRefOp and BorrowMutOp build methods. Default to unset (passes that don't set it still work).

**RegionInference.cpp:**
After `assignInitialRegions()` (line 116), iterate borrow ops and set `regionId` attribute:
```cpp
borrowOp->setAttr("regionId", builder.getI32IntegerAttr(region.id));
```

**RegionInferenceResult:**
Add query method: `Optional<RegionID> getRegionForBorrow(Value borrowVal)`.

### 6. Constraint Solving (Outlives)

**Problem:** RegionInference extends regions via BFS but doesn't build an outlives constraint graph. AliasCheck uses a `borrowsOverlap()` heuristic instead of checking region point sets. This misses cross-block lifetime violations.

**Files:**
- Modify: `lib/Analysis/RegionInference.cpp` (~100 LOC)
- Modify: `include/asc/Analysis/RegionInference.h` (~30 LOC)
- Modify: `lib/Analysis/AliasCheck.cpp` — rewrite overlap detection (~80 LOC)

**Changes:**

**RegionInference — Outlives Graph:**
```cpp
struct OutlivesConstraint {
    RegionID shorter;   // must not outlive...
    RegionID longer;    // ...this region
    mlir::Location loc; // source location for diagnostics
};
SmallVector<OutlivesConstraint> outlives;
```

Constraints generated from:
- Function calls: borrow arg region must outlive the call point
- Phi arguments: source region must outlive target block entry
- Return values: borrow region must outlive function scope

**Constraint validation:**
After region extension, verify each constraint: `shorter.lastPoint <= longer.lastPoint`. Violation → error E007 "borrow does not live long enough".

**AliasCheck — Region-Based Overlap:**
Replace `borrowsOverlap()` with:
```cpp
bool regionsOverlap(RegionID a, RegionID b, const RegionInferenceResult &regions) {
    // Check if any CFGPoint appears in both regions
    for (auto &pt : regions.getRegion(a).points)
        if (regions.getRegion(b).containsPoint(pt))
            return true;
    return false;
}
```

Import `RegionInference.h`, accept `RegionInferenceResult` as pass input.

**Tests:**
- `test/e2e/outlives_basic.ts` — borrow returned from inner scope → E007
- `test/e2e/outlives_call.ts` — borrow passed to function must outlive call
- `test/e2e/region_alias_cross_block.ts` — mutable borrow in one block, shared in successor → correct detection
- `test/e2e/region_alias_ok.ts` — non-overlapping borrows in sequence → passes

## Execution Strategy

```
Phase 1: 4 parallel agents in git worktrees
  ├── Agent A: LinearityCheck (item 1)
  ├── Agent B: Drop Flags (item 2)
  ├── Agent C: EscapeAnalysis (item 3)
  └── Agent D: PanicInfo (item 4)
  
  Each agent: implement → build → test → commit to worktree branch
  Merge all 4 branches to main sequentially

Phase 2: Sequential in main branch
  5. Region tokens (depends on merged Phase 1 for clean base)
  6. Constraint solving (depends on 5)
```

## New Error Codes

| Code | Severity | Description |
|------|----------|-------------|
| E005 | Error | Value never consumed (resource leak) |
| E006 | Error | Value consumed multiple times (double-free) |
| E007 | Error | Borrow does not live long enough (outlives violation) |
| W004 | Warning | Unnecessary @heap annotation (escape analysis: stack-safe) |

## Expected Coverage After Implementation

| RFC | Before | After | Delta |
|-----|--------|-------|-------|
| 0005 (Ownership) | 65% | 85% | +20% |
| 0006 (Borrow Checker) | 70% | 85% | +15% |
| 0008 (Memory) | 55% | 75% | +20% |
| 0009 (Panic) | 45% | 55% | +10% |
| **Overall weighted** | **58%** | **~68%** | **+10%** |

## Files Changed Summary

| File | Action | Items |
|------|--------|-------|
| `lib/Analysis/LinearityCheck.cpp` | New | 1 |
| `include/asc/Analysis/LinearityCheck.h` | New | 1 |
| `lib/Analysis/EscapeAnalysis.cpp` | New | 3 |
| `include/asc/Analysis/EscapeAnalysis.h` | New | 3 |
| `include/asc/HIR/OwnOps.h` | Modify | 2, 5 |
| `lib/HIR/OwnDialect.cpp` | Modify | 2 |
| `lib/Analysis/DropInsertion.cpp` | Modify | 2 |
| `lib/Analysis/RegionInference.cpp` | Modify | 5, 6 |
| `include/asc/Analysis/RegionInference.h` | Modify | 5, 6 |
| `lib/Analysis/AliasCheck.cpp` | Modify | 6 |
| `lib/CodeGen/OwnershipLowering.cpp` | Modify | 2, 3 |
| `lib/CodeGen/PanicLowering.cpp` | Modify | 4 |
| `lib/Runtime/runtime.c` | Modify | 4 |
| `lib/Driver/Driver.cpp` | Modify | 1, 3 |
| 14 test files | New | 1-6 |
