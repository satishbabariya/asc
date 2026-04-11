# Fix 6 Test-Discovered Issues — Design Spec

**Date:** 2026-04-11
**Goal:** Fix 6 bugs found during comprehensive feature testing.

---

## Issue 1: Native/Wasm O2 Legacy PM Crash

**Problem:** `addPassesToEmitFile` crashes at O2+ on both Wasm and native targets. Currently only Wasm has the O0-codegen workaround.

**Fix:** In `CodeGen.cpp:setupTargetMachine`, use `CodeGenOptLevel::None` for ALL targets. The new-PM in `runLLVMOptPasses()` handles optimization; the legacy PM only needs O0 for instruction selection and code emission.

**Files:** `lib/CodeGen/CodeGen.cpp`

## Issue 2: while let — Option reassignment

**Problem:** `x = Option::None` where `x: Option<i32>` fails with "assignment type mismatch" because `Option::None` resolves to the base `Option` type, not `Option_i32`.

**Fix:** In `Sema::checkAssignExpr`, when the RHS is a path expression resolving to an enum variant and the LHS type is a named type, check if the RHS enum name (before monomorphization) matches the LHS type's base name. If so, allow the assignment.

**Files:** `lib/Sema/SemaExpr.cpp`

## Issue 3: let-else — pattern variable scope

**Problem:** `let Option::Some(v) = opt else { return 0; }; return v;` — `v` is undeclared because Sema's let-statement handler doesn't bind pattern variables from enum destructuring.

**Fix:** In Sema's let-statement handling, when a `LetStmt` has a pattern (via `VarDecl::getPattern()`), bind the pattern variables into the current scope. For `EnumPattern`, extract the inner `IdentPattern` args and declare them with the appropriate types from the enum variant.

**Files:** `lib/Sema/SemaExpr.cpp` or `lib/Sema/Sema.cpp` (wherever let statements are checked)

## Issue 4: Generic struct literal return type

**Problem:** `return Pair { first: a, second: b }` where return type is `Pair<i32>` — the struct literal produces monomorphized type `Pair_i32` which doesn't match `Pair<i32>`.

**Fix:** In `Sema::isCompatible`, when comparing named types, strip monomorphization suffixes. If `Pair_i32` is compared with `Pair`, check if `Pair_i32` is a monomorphization of `Pair` by looking it up in the monoCache.

**Files:** `lib/Sema/SemaType.cpp`

## Issue 5: Wasm auto-link with runtime

**Problem:** Auto-linker doesn't compile or link the runtime, so there's no `_start` and exit codes don't propagate.

**Fix:** In `Driver::linkWasm`, before invoking wasm-ld, compile `runtime.c` to a temp .o using clang (found via LLVM install path). Include both the program .o and runtime .o in the wasm-ld invocation. Use `--export=_start` instead of `--no-entry`.

**Files:** `lib/Driver/Driver.cpp`

## Success Criteria

1. `while let` with `x = Option::None` compiles
2. `let Option::Some(v) = ... else { ... }; return v;` compiles — `v` in scope
3. Native target compiles at O2 without crashing
4. Generic struct literal return matches generic return type
5. `asc build foo.ts -o foo.wasm` produces runnable binary with correct exit code
6. All 168 existing tests still pass
