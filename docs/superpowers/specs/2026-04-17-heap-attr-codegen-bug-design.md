# @heap Codegen Bug — Missing Load on DeclRef

**Date:** 2026-04-17
**Status:** Design
**Depends on:** RFC-0008 §Explicit @heap attribute; PR #40; PR #41
**Motivating discovery:** Discovered during the 2026-04-17 RFC audit. `@heap` on a local binding produces malformed LLVM IR when the binding is later used in a return or function argument — the pointer is returned/passed where the underlying value is expected.

## Context

`visitLetDecl` in `lib/HIR/HIRBuilder.cpp` (around line 460) has two paths for mutable scalar `let` bindings:

- **Stack path** (default): `llvm.alloca` of the scalar's type. `AllocaOp::getElemType()` records the element type on the op.
- **Heap path** (`@heap` attribute): `llvm.call @malloc(sizeof)` → `llvm.ptr`. No element type is recorded on the call op.

Both paths `declare(d->getName(), storage)` where `storage` is a pointer. `visitDeclRefExpr` (line 801) is responsible for loading from the pointer when the variable is read.

The current `visitDeclRefExpr` load branch (lines 816-828) checks specifically for `AllocaOp` as the defining op:

```cpp
if (mlir::isa<mlir::LLVM::LLVMPointerType>(val.getType())) {
  auto *defOp = val.getDefiningOp();
  if (defOp && mlir::isa<mlir::LLVM::AllocaOp>(defOp)) {
    auto allocaOp = mlir::cast<mlir::LLVM::AllocaOp>(defOp);
    mlir::Type elemType = allocaOp.getElemType();
    if (elemType && (elemType.isIntOrIndexOrFloat() || ...)) {
      return builder.create<mlir::LLVM::LoadOp>(..., elemType, val);
    }
  }
}
return val;
```

Because the `@heap` defining op is `CallOp` (not `AllocaOp`), the load branch is skipped. The raw pointer is returned as the variable's value, which then mismatches downstream scalar-typed sites.

### Concrete repro

```typescript
fn main(): i32 {
  @heap
  let x: i32 = 42;
  return x;
}
```

Produces:

```
Function return type does not match operand type of return inst!
  ret ptr %1
 i32error: failed to translate MLIR to LLVM IR
```

MLIR output shows `func.return` returning a `!llvm.ptr` from a function with `() -> i32` signature.

### Scope of the bug

The same mechanism breaks any use of `@heap` scalars as rvalues:

- `return x` where `x` is `@heap` → malformed IR (return type mismatch).
- `f(x)` where `x` is `@heap` and `f` takes a scalar → pointer passed to scalar parameter.
- `y = x + 1` → pointer arithmetic where scalar is expected.

Drop insertion for `@heap` is **not** affected — `own.drop` correctly runs on the pointer, freeing the allocation. `own.try_scope`/`own.catch_scope` pair is emitted. The panic-unwind cleanup path is intact.

## Scope

Single-bug fix: extend `visitDeclRefExpr` to load from malloc-backed pointers the same way it already loads from alloca-backed pointers. This requires recording the element type on the malloc `CallOp` when it is created.

Out of scope: redesigning `@heap`, unifying alloca and malloc paths, escape analysis changes, or audit of other per-op-kind branches in `visitDeclRefExpr`.

## Design

### Two edits in `lib/HIR/HIRBuilder.cpp`

**Edit 1 — annotate the malloc call in `visitLetDecl`** (around line 486). Capture the intended element type as an MLIR `TypeAttr` named `asc.elem_type`:

```cpp
auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(location, i64Type, (int64_t)size);
auto mallocCall = builder.create<mlir::LLVM::CallOp>(
    location, mallocFn, mlir::ValueRange{sizeVal});
mallocCall->setAttr("asc.elem_type", mlir::TypeAttr::get(init.getType()));
storage = mallocCall.getResult();
```

Three new lines: split the `create<CallOp>` into a named local, `setAttr`, read `.getResult()`.

**Edit 2 — extend the load branch in `visitDeclRefExpr`** (around lines 816-828). Refactor to extract an element type from either `AllocaOp` or an annotated `CallOp`, then use a single load branch:

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

**Design notes:**

- The attribute namespace `asc.*` matches existing project convention for custom MLIR attributes (or will establish it; verify with a grep for `setAttr(\"asc.` during implementation — if no prior uses, this is a clean first instance).
- `TypeAttr` is the standard MLIR vehicle for storing `mlir::Type` on an op.
- The refactor to a single load branch preserves existing alloca-path behavior exactly — `elemType` is set the same way and the predicate is unchanged.
- Other `CallOp` results in the codebase will not match this path because they won't have the `asc.elem_type` attribute set. No false positives.

## Testing

### New lit tests in `test/e2e/`

| File | What it tests |
|---|---|
| `heap_local_return.ts` | `@heap let x: i32 = 42; return x;` — compiles to Wasm, runs on wasmtime, exits 42. |
| `heap_pass_to_fn.ts` | `fn double(x: i32): i32` called with `@heap` local, Wasm exit 42 (2×21). |
| `heap_mutate.ts` | `@heap let x = 1; x = 2; return x;` — confirms store/load both use the element type correctly. |

Each follows the existing RUN-pattern used in `test/e2e/heap_attr.ts`:

```
// RUN: %asc build %s --emit llvmir > %t.out 2>&1; grep -q "<expected-pattern>" %t.out
```

Build to LLVM IR, then grep for a correctness marker. For these tests the marker is a correctly-typed load before return (e.g., `grep -q "load i32, ptr"` proves the scalar was loaded from the heap pointer, not returned raw). Before Edit 2, `asc build --emit llvmir` itself fails (MLIR→LLVM translation error), so the test command fails — proving the bug.

### Regression guard

`lit test/ --no-progress-bar` must continue to show **275 passing** plus the 3 new tests → **278 passing**, zero failures.

### Success criteria

- 3 new lit tests pass.
- The repro from §Context no longer produces the "Function return type does not match" error; it compiles and runs.
- Zero regressions on the existing 275 tests.

## Out of scope

- Redesigning `@heap` (e.g., unifying with `alloca` via marker attribute + lowering pass) — cleaner long-term but scope creep.
- Extending the load branch to handle additional pointer-producing ops beyond `AllocaOp` and annotated `CallOp`. If other patterns surface empirically, handle them in a follow-up with their own tests.
- `@heap` on aggregate types (`@heap let x = SomeStruct { ... }`) — the `visitLetDecl` heap path at line 475 is reached only when the gate at line 463 is passed (`isIntOrIndexOrFloat() || isa<LLVMPointerType>(...)`). Aggregate `@heap` goes through a separate later path in `visitLetDecl` (around line 511) that decorates `own.alloc` with a `heap` attribute and is handled by OwnershipLowering; it already works (see `test/e2e/heap_attr.ts` for the aggregate case). This bug is strictly about the scalar path.
- `@heap` interaction with `own<T>` parameters at function boundaries — not affected by this bug; the current repros are all local-variable-as-rvalue.

## Risk

Low. Two small edits, localized to one file, follow an existing pattern (attribute-based op metadata). The refactor preserves alloca-path behavior by construction (same source of `elemType`, same predicate). The new `asc.elem_type` attribute name is fresh — no conflict. The worst case is a silent false-positive where another `CallOp` happens to have `asc.elem_type` set and triggers an unintended load; this is not possible today because no other code path sets this attribute.
