# Derive(Clone/PartialEq) HIR Dispatch Fix + Derive(Default)

| Field | Value |
|---|---|
| Date | 2026-04-17 |
| Goal | Make `@derive(Clone)`, `@derive(PartialEq)`, `@derive(Default)` produce real, dispatched method calls; replace today's silent aliasing with verifiable copies and comparisons |
| Baseline | 256 lit tests passing. Sema synthesis from `2026-04-16-derive-expansion-design.md` exists but synthesized methods never reach MLIR — `.clone()` aliases, `.eq()` is a no-op |
| Target | `@derive(Clone)` emits `Type_clone` and main calls it; `@derive(PartialEq)` emits `Type_eq` and main calls it; `@derive(Default)` emits `Type_default` and main calls it. All verified by IR-grep + runtime tests |

## Background

`docs/superpowers/specs/2026-04-16-derive-expansion-design.md` proposed AST-level synthesis of impl blocks for `@derive(Clone)` and `@derive(PartialEq)`. Commit `1144935` implemented `synthesizeDeriveImpls()` in `lib/Sema/Sema.cpp:47-158`. Tests `test/e2e/derive_clone.ts` and `test/e2e/derive_partialeq.ts` pass — but only run `%asc check`, which never reaches HIRBuilder.

Manual investigation (this spec) revealed:

1. **`@derive(Clone)` aliases instead of copying.** A test `let q = p.clone(); return q.x` produces MLIR that reads from `p`'s underlying pointer, not from a separate clone. The `clone` intrinsic at `lib/HIR/HIRBuilder.cpp:2347-2372` checks for `LLVMPointerType` receivers, but synthesized struct receivers arrive as `own.val`. The intrinsic falls through to `return receiver` — pure aliasing. For heap-backed types (String, Vec) this is a use-after-free hazard.

2. **`@derive(PartialEq)` is a silent no-op.** A test where `a.r != c.r` and `if a.eq(&c) { return 99 }` returns 0 — no comparison, no branch in the IR.

3. **Synthesized `Type_clone` / `Type_eq` functions are not emitted at all.** A control test with a manual `impl Counter { fn doubled(...) }` produces both `doubled` and `Counter_doubled` symbols and a working `func.call`. The same pattern via `@derive` produces neither symbol. Either `visitImplDecl` is not called for synthesized impls, or the synthesized AST is malformed in some way that prevents emission.

The generic dispatch fallback at `lib/HIR/HIRBuilder.cpp:3425-3433` already supports `TypeName_method` lookup — it works for manual impls. Once the synthesized functions actually reach MLIR and the `clone` intrinsic is taught to defer to user impls, this fallback handles the rest.

## Scope

### In scope

- Diagnose root cause for synthesized impls not reaching MLIR (one of: ASTVisitor dispatch, malformed synthesized nodes, Sema not appending to the live items vector, or impl bodies missing required wiring).
- Fix the root cause so `@derive(Clone)`/`@derive(PartialEq)` produce `Type_clone`/`Type_eq` symbols.
- Reorder the `clone` intrinsic at HIRBuilder.cpp:2347 to defer to user impls (lookup `TypeName_clone` first; fall through to scalar/Arc handling).
- Extend `synthesizeDeriveImpls()` with `@derive(Default)` for structs whose fields are all primitive (numeric, bool, char). Non-primitive field types emit `ErrTraitNotImplemented`.
- Strengthen tests: convert `derive_clone.ts` / `derive_partialeq.ts` to `%asc build --emit mlir` + grep for the synthesized function and call site. Add a runtime-style test that catches aliasing by checking field values from a clone of a heap-backed type.
- Add `derive_default.ts` runtime + IR test.
- Update `CLAUDE.md` to reflect actual derive support.

### Out of scope

- `@derive(Debug)` — needs Formatter type and string concatenation infra.
- `@derive(Hash)` — needs Hasher infra.
- `@derive(Eq)` (the marker trait beyond PartialEq) — already a marker, no method to synthesize.
- Enum derives.
- Generic struct derives (e.g., `@derive(Clone) struct Pair<T> { ... }`).

## Implementation outline

### Phase 1: Diagnose & fix emission (the tap)

The plan's Task 1 is a writeable IR-grep test that asserts `Counter_clone` appears in the emitted MLIR. It will fail. Task 2 traces why through targeted printf or a small probe; Task 3 fixes it. Likely candidates in order of probability:

1. **`visitImplDecl` checks `traitType` against a list and skips Clone/PartialEq** — quick check by reading current behavior.
2. **`emitFunctionBody` fails silently** when synthesized FunctionDecl bodies reference `self` without it being in the symbol table — synthesized methods set `isSelfRef` on `ParamDecl` but never call into Sema to resolve types. The synthesized body's expressions never have `setType()` called, so `visitMethodCallExpr` and `visitFieldAccessExpr` see `getType() == nullptr` and bail.
3. **The synthesized impl is appended to `items` but Sema's checkImplDecl never gets called for it** because `synthesizeDeriveImpls` runs before the type registration pass — but type registration loop iterates the same vector, so this should still pick it up.

Most likely culprit: hypothesis #2. The synthesized AST nodes have no resolved types because they bypass Sema's expression checker. Fix: after synthesis, run synthetic impls through `checkImplDecl` (or refactor to set types during synthesis).

### Phase 2: Reorder the `clone` intrinsic

Move the user-impl lookup at HIRBuilder.cpp:3425-3433 to fire BEFORE the clone intrinsic at line 2347. Or: inside the clone intrinsic, prepend a lookup for `TypeName_clone` and call it if present.

Choose: **prepend a lookup inside the clone intrinsic.** Less invasive, doesn't risk regressing Arc/scalar paths.

### Phase 3: Add Derive(Default)

For each primitive field, generate a default literal:

| Field type | Default expression |
|---|---|
| i8/i16/i32/i64/i128/u*/usize/isize | `IntegerLiteral(0)` |
| f32/f64 | `FloatLiteral(0.0)` |
| bool | `BoolLiteral(false)` |
| char | `CharLiteral(0)` |
| Other (NamedType, etc.) | Diagnostic: "derive(Default) requires all fields to be primitive types" |

Synthesis pattern mirrors Clone: build `StructLiteral` with default-valued fields, wrap in `ReturnStmt`, build `FunctionDecl` named `default`, wrap in `ImplDecl` for `Default`.

Note: `default` is a static method (no `self`), so the ParamDecl list is empty.

### Phase 4: Tests

- `test/e2e/derive_clone.ts` — change to `%asc build %s --emit mlir > %t.out 2>&1; grep -q "Counter_clone" %t.out; grep -q "func.call.*Counter_clone" %t.out`
- `test/e2e/derive_partialeq.ts` — analogous for `Color_eq`
- `test/e2e/derive_clone_runtime.ts` — new: build to wasm, run via wasmtime if available; otherwise validate via `--emit llvmir` patterns. Use a struct with multiple fields and verify the clone has independent storage.
- `test/e2e/derive_default.ts` — new: IR-grep for `Counter_default` symbol + runtime check exit code.
- Edge case: `test/e2e/derive_empty_struct.ts` — `@derive(Clone, PartialEq, Default) struct Empty {}` should produce trivially-true `eq` and constant `default`.

## Risks

- **Hypothesis #2 might be wrong.** If the bug is elsewhere (e.g., a guard in visitImplDecl), the fix changes shape. The plan's Task 2 (diagnose) explicitly produces a fix-shape decision; subsequent tasks are gated on it.
- **Type resolution for synthesized AST.** If we route synthetic impls through Sema's `checkImplDecl`, the synthesized expressions need to type-check successfully. `self.field_name` requires Sema's struct field lookup to work on the synthesized `FieldAccessExpr` — should already work since the underlying machinery is the same.
- **Existing tests may regress.** `clone_eq.ts` (separate from `derive_clone.ts`) calls `.clone()` on a struct without `@derive(Clone)`. Today this aliases successfully. After the fix, since the struct has no impl, the intrinsic still fires its scalar/struct memcpy path — this test should keep passing. Verify in Task 5.
- **Lit tests don't have wasmtime guaranteed.** The IR-grep approach is portable and matches existing patterns (`mpmc_channel.ts`, `scoped_thread.ts`). Use that primarily; defer wasmtime-based tests if not in CI.

## Acceptance

1. `lit test/` — 256+ tests pass (no regressions, plus new tests).
2. `grep -q Counter_clone` against `--emit mlir` output of any `@derive(Clone)` struct succeeds.
3. `grep -q Color_eq` against `--emit mlir` output of any `@derive(PartialEq)` struct succeeds.
4. `grep -q Counter_default` against `--emit mlir` output of any `@derive(Default)` struct succeeds.
5. CLAUDE.md "Traits" section accurately reflects which derives are implemented end-to-end.
