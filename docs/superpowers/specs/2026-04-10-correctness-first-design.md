# Correctness-First Push — Design Spec

**Date:** 2026-04-10
**Approach:** Bottom-Up (Lowering → Borrow Checker → Type System → Architecture)
**Goal:** Fix silent miscompilation and soundness holes so existing features produce correct results.

---

## Section 1: Ownership Lowering Fixes

### 1a. Heap vs Stack allocation for `own.alloc`

**Problem:** `own.alloc` always emits `llvm.alloca` (stack). Heap-allocated values (e.g., `Box::new()`) are miscompiled.

**Fix:**
- In HIRBuilder, tag `own.alloc` ops with a `"heap"` unit attribute when emitting for `Box::new()` or other explicit heap allocations.
- In `OwnershipLoweringPass`, check for the `"heap"` attribute:
  - Present → emit `llvm.call @malloc(sizeof(T))` + store value into allocated memory.
  - Absent → emit `llvm.alloca` (existing behavior for local bindings).
- The `getOrInsertMalloc` helper already exists in OwnershipLowering.cpp but is never called — wire it up.

**Files:** `lib/HIR/HIRBuilder.cpp`, `lib/CodeGen/OwnershipLowering.cpp`

### 1b. Aggregate `own.move` via memcpy

**Problem:** `own.move` does SSA value forwarding for all types. For LLVM struct types (aggregates passed by pointer), this loses the data — the source pointer is forwarded but no data is copied to the destination.

**Fix:**
- In `OwnershipLoweringPass`, when lowering `own.move`:
  - Check if the operand type is `mlir::LLVM::LLVMStructType` (aggregate).
  - If aggregate: emit `llvm.memcpy(dst, src, sizeof(T))` then invalidate the source.
  - If scalar/pointer: continue SSA forwarding (existing behavior).

**Files:** `lib/CodeGen/OwnershipLowering.cpp`

### 1c. Borrow op pointer semantics

**Problem:** `own.borrow_ref` and `own.borrow_mut` forward operands via SSA alias. For aggregate types where the operand is a pointer, this is correct for whole-struct borrows but the LLVM types on both sides must match.

**Fix:**
- Verify that the result type of the lowered borrow op matches the operand type.
- If there's a type mismatch (e.g., the borrow narrows to a field), emit a GEP.
- For whole-struct borrows (the common case), SSA forwarding of the pointer is correct — just ensure types align.

**Files:** `lib/CodeGen/OwnershipLowering.cpp`

### 1d. Send/Sync type parameters on OwnValType

**Problem:** `OwnValType::isSend()` returns `true` and `isSync()` returns `false` unconditionally (hardcoded in OwnTypes.h). `SendSyncCheck` reads these values, making it structurally present but functionally vacuous.

**Fix:**
- Update `OwnValType` type storage to store `innerType` (mlir::Type), `sendFlag` (bool), `syncFlag` (bool).
- Update `OwnValType::get()` factory to accept these parameters.
- In HIRBuilder, when emitting `own.alloc` for a struct type, propagate `@send`/`@sync` attributes (already validated in SemaDecl.cpp:50-88) into the `OwnValType` parameters.
- `SendSyncCheck` then reads real values from the type.

**Files:** `include/asc/HIR/OwnTypes.h`, `lib/HIR/OwnTypes.cpp`, `lib/HIR/HIRBuilder.cpp`, `lib/Analysis/SendSyncCheck.cpp`

---

## Section 2: Borrow Checker Soundness Fixes

### 2a. Op name consistency

**Problem:** RegionInference searches for `"own.borrow"`, AliasCheck searches for `"own.borrow_ref"`. The registered op name is `own.borrow_ref`. Pass 2 silently misses all borrow ops.

**Fix:** Update RegionInference.cpp to use `"own.borrow_ref"` and `"own.borrow_mut"` consistently. Grep all analysis passes for any other string-based op name references and standardize.

**Files:** `lib/Analysis/RegionInference.cpp`, verify all files in `lib/Analysis/`

### 2b. Type detection via `mlir::isa<>`

**Problem:** All analysis passes detect owned/borrow types via `type.getAbstractType().getName().contains("own.val")` — string matching. MoveCheck.cpp also has a dead `type.dyn_cast<mlir::Type>()` that always succeeds.

**Fix:**
- Replace all `getName().contains("own.val")` with `mlir::isa<own::OwnValType>(type)`.
- Replace all `getName().contains("borrow")` with `mlir::isa<own::BorrowType>(type)` or `mlir::isa<own::BorrowMutType>(type)`.
- Remove the dead `dyn_cast<mlir::Type>()` in MoveCheck.cpp.
- Update `isOwnedType()` / `isBorrowType()` helpers across: MoveCheck, AliasCheck, DropInsertion, PanicScopeWrap, SendSyncCheck.

**Files:** All files in `lib/Analysis/`

### 2c. Pass 2 — Enable phi-node region propagation

**Problem:** `propagateThroughPhis()` body is wrapped in `if (false)` with comment about MLIR 18. The outlives constraint at control flow joins is never enforced.

**Fix:**
- Remove the `if (false)` guard.
- Verify the successor-operand propagation works with MLIR 18's block argument API.
- If the API has changed, update to use `block->getSuccessors()` and `BranchOpInterface` to iterate successor block arguments.
- Test with a program that borrows a value, branches (if/else), and uses the borrow after the join.

**Files:** `lib/Analysis/RegionInference.cpp`

### 2d. Pass 2 — CFG-aware cross-block region extension

**Problem:** Cross-block region extension uses `min(defBlock, useBlock)` to `max(defBlock, useBlock)` linear index range. Incorrect for loops and non-linear CFGs.

**Fix:**
- Replace linear index range with BFS/DFS reachability from the def block to the use block.
- Compute the set of blocks on any path from def to use.
- Extend the borrow's region to include all blocks in this reachable set.
- Use MLIR's `mlir::Block::getSuccessors()` for CFG traversal.
- Cache reachability results per function to avoid redundant computation.

**Files:** `lib/Analysis/RegionInference.cpp`

### 2e. Pass 3 — Region-based cross-block overlap detection

**Problem:** `borrowsOverlap()` returns `true` unconditionally for borrows in different blocks, producing false positives.

**Fix:**
- Replace the unconditional `true` with actual region overlap: two borrows overlap if their regions (computed in Pass 2) share any common block.
- With 2c and 2d fixed, region data is accurate.
- If region data is unavailable (defensive), fall back to conservative `true`.

**Files:** `lib/Analysis/AliasCheck.cpp`

### 2f. Pass 4 — Conditional move severity

**Problem:** Conditional move calls `signalPassFailure()` (hard error). RFC specifies a warning + drop-flag insertion.

**Fix:**
- Change `MaybeMoved` handling from `signalPassFailure()` to emitting a warning diagnostic.
- Drop-flag insertion (RFC-0008) is deferred — out of scope for this push.
- Programs with conditional moves will compile with a warning instead of being rejected.

**Files:** `lib/Analysis/MoveCheck.cpp`

---

## Section 3: Type System & Sema Fixes

### 3a. Integer type compatibility

**Problem:** `isCompatible()` allows any integer to any integer (i8 ↔ u64).

**Fix:**
- Allow implicit widening within same signedness: i8 → i16 → i32 → i64 → i128, u8 → u16 → u32 → u64 → u128.
- Allow same-width signed ↔ unsigned: i32 ↔ u32 (for practical interop).
- Allow usize ↔ u64 and isize ↔ i64 (platform equivalence).
- All other integer conversions require explicit `as` cast.
- Float widening f32 → f64 remains allowed.

**Files:** `lib/Sema/SemaType.cpp` (in `isCompatible()`)

### 3b. `?` operator desugaring

**Problem:** `checkTryExpr()` passes operand type through unchanged.

**Fix:**
- If operand type is `Result<T, E>`: expression type is `T`, enclosing function must return `Result<_, E>` (or compatible). Emit error if function return type is not Result.
- If operand type is `Option<T>`: expression type is `T`, enclosing function must return `Option<_>`. Emit error if function return type is not Option.
- If operand is neither Result nor Option: emit error "? operator requires Result or Option".

**Files:** `lib/Sema/SemaExpr.cpp` (in `checkTryExpr()`)

### 3c. `for` loop iterator unwrapping

**Problem:** `checkForExpr()` assigns raw iterable type to loop variable.

**Fix:**
- Special-case known iterable types:
  - `Vec<T>` → loop variable is `T`
  - `Array<T, N>` / `[T; N]` → loop variable is `T`
  - Range expressions (i32..i32) → loop variable is the range element type
  - `String` → loop variable is `char`
- Full `IntoIterator` trait resolution deferred to later.

**Files:** `lib/Sema/SemaExpr.cpp` (in `checkForExpr()`)

### 3d. Match exhaustiveness — basic variant coverage

**Problem:** No exhaustiveness checking exists.

**Fix:**
- After checking all match arms, collect the set of matched enum variant names.
- If the matched type is an enum and no wildcard `_` arm exists, verify all variants are covered.
- Special-case `Option` (must cover `Some` + `None`) and `Result` (must cover `Ok` + `Err`).
- Emit a warning (not error) for non-exhaustive matches.
- Do not attempt nested pattern analysis — top-level variant coverage only.

**Files:** `lib/Sema/SemaExpr.cpp` (in `checkMatchExpr()`)

---

## Section 4: Architectural Fixes

### 4a. Re-enable MLIR verifier

**Problem:** `pm.enableVerifier(false)` in Driver::runAnalysis() and CodeGenerator::runMLIRLowering().

**Fix:** Change to `pm.enableVerifier(true)` in both locations. If this exposes existing pass bugs, fix them as part of Sections 1-2.

**Files:** `lib/Driver/Driver.cpp`, `lib/CodeGen/CodeGen.cpp`

### 4b. Default optimization level to O2

**Problem:** `DriverOptions::optLevel` defaults to `O0`. RFC-0003 specifies O2.

**Fix:** Change default in `Driver.h` from `OptLevel::O0` to `OptLevel::O2`.

**Files:** `include/asc/Driver/Driver.h`

### 4c. Debug info source file

**Problem:** `addDebugInfo()` uses output file path as debug source filename.

**Fix:** Pass input source path through `CodeGenOptions`. Use it for DICompileUnit filename and directory.

**Files:** `lib/CodeGen/CodeGen.cpp`, `include/asc/CodeGen/CodeGen.h`

---

## Out of Scope

- `own.try_scope`/`own.catch_scope` cleanup emission (Wasm EH / setjmp unwind)
- Drop-flag insertion for conditional moves (RFC-0008)
- `ConversionTarget`/`RewritePatternSet` migration
- Static-to-instance refactor for Driver globals
- Syntax gaps (`if let`, `while let`, or-patterns, let-else)
- Standard library additions

## Success Criteria

1. All 118 existing e2e tests still pass.
2. `Box::new()` emits `malloc` (verifiable via `--emit llvmir`); local structs emit `alloca`.
3. Aggregate `own.move` emits `memcpy` for struct types.
4. `OwnValType` carries real Send/Sync flags; `SendSyncCheck` reads them.
5. Borrow checker rejects cross-block aliasing violations without false positives on sequential scoped borrows.
6. Op name `"own.borrow_ref"` is consistent across all analysis passes.
7. Type detection uses `mlir::isa<>` in all analysis passes.
8. `let x: i8 = some_u64_value;` produces a type error without explicit cast.
9. `?` on non-Result/non-Option produces an error.
10. Non-exhaustive enum match produces a warning.
11. MLIR verifier is enabled and all passes produce valid IR.
12. Default `--opt` is O2.

## Priority Order

1. Section 1 (Ownership Lowering) — silent miscompilation, highest risk
2. Section 2 (Borrow Checker) — soundness holes, second-highest risk
3. Section 3 (Type System) — wrong acceptance, third priority
4. Section 4 (Architecture) — low-effort quality improvements, do alongside
