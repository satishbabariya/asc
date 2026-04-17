# @heap Scalar Codegen Bug Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix `visitDeclRefExpr` in `lib/HIR/HIRBuilder.cpp` so that `@heap`-allocated scalar variables are loaded from their malloc pointer when read, not returned raw.

**Architecture:** Two edits in one file. Edit 1: annotate the `malloc` `CallOp` in `visitLetDecl`'s `@heap` path with an `asc.elem_type` `TypeAttr` capturing the scalar's element type. Edit 2: extend the pointer-load branch in `visitDeclRefExpr` to read the attribute from annotated `CallOp`s (in addition to the existing `AllocaOp` path) and emit a `LoadOp`.

**Tech Stack:** C++ (LLVM 18 / MLIR 18), CMake, lit.

---

## File Structure

**Modified files:**
- `lib/HIR/HIRBuilder.cpp` — two edits in two adjacent methods:
  - `visitLetDecl` at line ~486 (3 lines added; split the CallOp creation, set attr, read result).
  - `visitDeclRefExpr` at lines ~816-828 (refactor to extract `elemType` from either `AllocaOp` or annotated `CallOp` then single load branch; net +~5 LOC).

**New test files (all under `test/e2e/`):**
- `heap_scalar_return.ts` — `@heap let x: i32 = 42; return x;` compiles clean, LLVM IR contains a load before the return.
- `heap_scalar_pass_to_fn.ts` — `@heap let x: i32 = 21; return double(x);` compiles clean, argument is loaded before call.
- `heap_scalar_mutate.ts` — `@heap let x = 1; x = 2; return x;` compiles clean, load+store+load sequence is present.

## Pre-flight: baseline and branch

```bash
cd /Users/satishbabariya/Desktop/asc
git checkout main && git pull
git checkout -b heap-codegen-bugfix
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -3
```

Expected: 275 passing (post-PR #41 baseline).

Sanity-check the bug still reproduces before writing a fix:

```bash
cat > /tmp/heap_repro.ts << 'EOF'
fn main(): i32 {
  @heap
  let x: i32 = 42;
  return x;
}
EOF
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" ./build/tools/asc/asc build /tmp/heap_repro.ts --target aarch64-apple-darwin --emit llvmir 2>&1 | head -3
```

Expected output:
```
Function return type does not match operand type of return inst!
  ret ptr %1
 i32error: failed to translate MLIR to LLVM IR
```

If this error does NOT appear, the bug has been fixed by something else and this plan is stale — stop and investigate.

---

## Task 1: Add failing test for @heap scalar return

TDD: write failing test first so the fix is validated by it.

**Files:**
- Create: `test/e2e/heap_scalar_return.ts`

- [ ] **Step 1: Write the test file**

Create `test/e2e/heap_scalar_return.ts`:

```typescript
// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "load i32, ptr" %t.out
// RUN: grep -q "ret i32" %t.out
// Test: @heap scalar variable is loaded before return, not returned as raw ptr.

fn main(): i32 {
  @heap
  let x: i32 = 42;
  return x;
}
```

The first RUN line builds to LLVM IR and captures stdout+stderr. Before the fix, the build fails with "Function return type does not match" — so nothing useful lands in `%t.out` (the error goes to stderr but MLIR→LLVM translation aborts before full IR emission). After the fix, `%t.out` contains the full valid IR.

The two grep RUN lines require the IR to contain a `load i32` from a pointer AND a `ret i32`. Both must hold for the test to pass.

- [ ] **Step 2: Run test to verify it FAILS**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/heap_scalar_return.ts -v 2>&1 | tail -10
```

Expected: FAIL. The build errors out, so `grep -q "load i32, ptr"` finds nothing and lit reports FAIL.

- [ ] **Step 3: Commit the failing test**

```bash
git add test/e2e/heap_scalar_return.ts
git commit -m "$(cat <<'EOF'
test(codegen): failing test for @heap scalar return

Currently fails because visitDeclRefExpr does not load from malloc-backed
pointers. Fix in next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Committing the red test is intentional — it documents the bug at the point of the fix in git history.

---

## Task 2: Annotate malloc CallOp with element type (Edit 1)

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp` (around lines 485-487 — the malloc call inside `visitLetDecl`)

- [ ] **Step 1: Locate the code to change**

Open `lib/HIR/HIRBuilder.cpp`. Find the block around line 485-487 inside `visitLetDecl`:

```cpp
        auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(location, i64Type, (int64_t)size);
        storage = builder.create<mlir::LLVM::CallOp>(
            location, mallocFn, mlir::ValueRange{sizeVal}).getResult();
```

- [ ] **Step 2: Split the call creation and add the attribute**

Replace the block above with:

```cpp
        auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(location, i64Type, (int64_t)size);
        auto mallocCall = builder.create<mlir::LLVM::CallOp>(
            location, mallocFn, mlir::ValueRange{sizeVal});
        mallocCall->setAttr("asc.elem_type", mlir::TypeAttr::get(init.getType()));
        storage = mallocCall.getResult();
```

Rationale: separating the `create<CallOp>` into a named local lets us call `setAttr` before extracting the result. `asc.elem_type` is a fresh attribute name (grep-verified no prior use). `mlir::TypeAttr::get` is the standard way to store an `mlir::Type` on an op. `init.getType()` is the scalar's MLIR type — the same type that goes into the subsequent `llvm.store`.

- [ ] **Step 3: Rebuild to confirm it still compiles**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -5
```

Expected: build succeeds. No new warnings. This edit alone does not fix the bug — the annotation is only useful once Task 3's consumer reads it.

- [ ] **Step 4: Sanity-check the failing test is still failing**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/heap_scalar_return.ts -v 2>&1 | tail -5
```

Expected: still FAILS (same as after Task 1). The attribute is being set but not read yet.

- [ ] **Step 5: Commit**

```bash
git add lib/HIR/HIRBuilder.cpp
git commit -m "$(cat <<'EOF'
hir: annotate @heap malloc CallOp with asc.elem_type

Records the scalar element type as a TypeAttr on the malloc call so that
visitDeclRefExpr (next commit) can emit the correct load.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Extend DeclRef load branch to handle annotated CallOp (Edit 2)

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp` (around lines 816-828 — the pointer-load branch inside `visitDeclRefExpr`)

- [ ] **Step 1: Locate the code to change**

Open `lib/HIR/HIRBuilder.cpp`. Find the block in `visitDeclRefExpr` around lines 816-828:

```cpp
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(val.getType())) {
    auto *defOp = val.getDefiningOp();
    if (defOp && mlir::isa<mlir::LLVM::AllocaOp>(defOp)) {
      auto allocaOp = mlir::cast<mlir::LLVM::AllocaOp>(defOp);
      mlir::Type elemType = allocaOp.getElemType();
      if (elemType && (elemType.isIntOrIndexOrFloat() ||
          mlir::isa<mlir::LLVM::LLVMPointerType>(elemType))) {
        return builder.create<mlir::LLVM::LoadOp>(
            builder.getUnknownLoc(), elemType, val);
      }
    }
  }
```

- [ ] **Step 2: Refactor to handle both AllocaOp and annotated CallOp**

Replace the block above with:

```cpp
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(val.getType())) {
    auto *defOp = val.getDefiningOp();
    mlir::Type elemType;
    if (defOp && mlir::isa<mlir::LLVM::AllocaOp>(defOp)) {
      elemType = mlir::cast<mlir::LLVM::AllocaOp>(defOp).getElemType();
    } else if (defOp && mlir::isa<mlir::LLVM::CallOp>(defOp)) {
      if (auto attr = defOp->getAttrOfType<mlir::TypeAttr>("asc.elem_type"))
        elemType = attr.getValue();
    }
    if (elemType && (elemType.isIntOrIndexOrFloat() ||
        mlir::isa<mlir::LLVM::LLVMPointerType>(elemType))) {
      return builder.create<mlir::LLVM::LoadOp>(
          builder.getUnknownLoc(), elemType, val);
    }
  }
```

Rationale: the original nested `if` for `AllocaOp` is lifted up so both the alloca path and a new `CallOp`-with-attribute path can feed the same element-type variable. The final predicate and `LoadOp` emission are unchanged, so the alloca path behaves identically. Only `CallOp`s with the `asc.elem_type` attribute (set exclusively by the `@heap` path) trigger the new branch — other call results pass through untouched.

- [ ] **Step 3: Rebuild**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -5
```

Expected: build succeeds.

- [ ] **Step 4: Verify the original repro now works**

Run:
```bash
cat > /tmp/heap_repro.ts << 'EOF'
fn main(): i32 {
  @heap
  let x: i32 = 42;
  return x;
}
EOF
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" ./build/tools/asc/asc build /tmp/heap_repro.ts --target aarch64-apple-darwin --emit llvmir 2>&1 | grep -E "load i32|ret i32|Function return" | head -5
```

Expected: output contains `load i32, ptr` and `ret i32`. No "Function return type does not match" error.

- [ ] **Step 5: Verify the failing test now passes**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/heap_scalar_return.ts -v 2>&1 | tail -5
```

Expected: PASS.

- [ ] **Step 6: Run full suite to confirm no regressions**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -5
```

Expected: `Passed: 276` (275 baseline + 1 new). Zero failures.

If any existing test fails — especially `heap_attr.ts` (the aggregate @heap test) — investigate before committing. The refactor preserves alloca-path behavior by construction, so regressions would indicate an unexpected interaction.

- [ ] **Step 7: Commit**

```bash
git add lib/HIR/HIRBuilder.cpp
git commit -m "$(cat <<'EOF'
hir: load @heap scalar through asc.elem_type attribute

visitDeclRefExpr now loads the element value from malloc-backed pointers
when the defining CallOp carries an asc.elem_type TypeAttr, symmetric
with the existing AllocaOp.getElemType() path. Fixes "Function return
type does not match" errors on @heap scalar reads.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Add test for @heap pass-to-function

**Files:**
- Create: `test/e2e/heap_scalar_pass_to_fn.ts`

- [ ] **Step 1: Write the test**

Create `test/e2e/heap_scalar_pass_to_fn.ts`:

```typescript
// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "load i32, ptr" %t.out
// RUN: grep -q "call.*@double.*i32" %t.out
// Test: @heap scalar passed as function argument is loaded at the call site.

fn double_it(x: i32): i32 {
  return x * 2;
}

fn main(): i32 {
  @heap
  let x: i32 = 21;
  return double_it(x);
}
```

- [ ] **Step 2: Run the test**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/heap_scalar_pass_to_fn.ts -v 2>&1 | tail -5
```

Expected: PASS (Task 3's fix covers this case — the argument is a DeclRefExpr read that gets loaded).

- [ ] **Step 3: Run full suite**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -3
```

Expected: `Passed: 277`.

- [ ] **Step 4: Commit**

```bash
git add test/e2e/heap_scalar_pass_to_fn.ts
git commit -m "$(cat <<'EOF'
test(codegen): @heap scalar as function argument

Regression guard for the load-on-DeclRef fix — confirms argument reads
of @heap scalars are loaded, not passed as raw pointers.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Add test for @heap scalar mutation

**Files:**
- Create: `test/e2e/heap_scalar_mutate.ts`

- [ ] **Step 1: Write the test**

Create `test/e2e/heap_scalar_mutate.ts`:

```typescript
// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "store i32 2, ptr" %t.out
// RUN: grep -q "load i32, ptr" %t.out
// Test: @heap scalar mutation stores to the heap slot; subsequent read loads from it.

fn main(): i32 {
  @heap
  let x: i32 = 1;
  x = 2;
  return x;
}
```

Before any fix this would fail for the same reason as Task 1. After Task 3's fix, both the assignment (`x = 2`) and the read (`return x`) use the pointer correctly — the store writes through it and the subsequent DeclRef loads from it.

- [ ] **Step 2: Run the test**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/heap_scalar_mutate.ts -v 2>&1 | tail -5
```

Expected: PASS.

If this FAILS, the assignment path (a separate code path from DeclRefExpr — it writes to the pointer via `llvm.store`) may have its own type-handling issue. In that case, file a separate issue referencing this task; do NOT try to fix the assignment path in this PR.

- [ ] **Step 3: Run full suite**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -3
```

Expected: `Passed: 278`.

- [ ] **Step 4: Commit**

```bash
git add test/e2e/heap_scalar_mutate.ts
git commit -m "$(cat <<'EOF'
test(codegen): @heap scalar mutation store+load roundtrip

Regression guard — confirms mutable @heap scalars store to the heap slot
and subsequent reads load from it correctly.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Full suite sanity check

Final verification.

- [ ] **Step 1: Run full suite**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | grep -E "Passed|Failed|Unresolved" | head -3
```

Expected: `Passed: 278 (100.00%)`. No other status lines.

- [ ] **Step 2: Confirm existing @heap aggregate test still passes**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/heap_attr.ts -v 2>&1 | tail -3
```

Expected: PASS. The aggregate @heap path is separate from the scalar path we fixed — this test existed before and should still pass, confirming no collateral damage.

- [ ] **Step 3: Confirm the bug is gone without a test in the loop**

Run:
```bash
cat > /tmp/heap_final_check.ts << 'EOF'
fn main(): i32 {
  @heap
  let x: i32 = 42;
  return x;
}
EOF
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" ./build/tools/asc/asc build /tmp/heap_final_check.ts --target aarch64-apple-darwin --emit llvmir 2>&1 | grep -E "ret i32|Function return" | head -3
```

Expected: output contains `ret i32 %<something>` and no "Function return type does not match" error.

No commit — pure verification.

---

## Summary of outputs

When all tasks complete:

- 5 commits on `heap-codegen-bugfix` branch:
  - Task 1: failing test (documents bug)
  - Task 2: annotate malloc CallOp
  - Task 3: fix DeclRef load branch (makes Task 1 test pass)
  - Task 4: pass-to-fn test
  - Task 5: mutation test
- `lib/HIR/HIRBuilder.cpp`: +3 LOC in `visitLetDecl` heap path; +~5 LOC refactor in `visitDeclRefExpr`.
- 3 new lit tests covering return, argument passing, and mutation for `@heap` scalars.
- `lit test/` total: **275 → 278 passing**.
- No changes to trait registration count (38), diagnostic IDs, or other surfaces.
