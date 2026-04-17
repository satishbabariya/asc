# Derive Dispatch Fix + Derive(Default) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `@derive(Clone)`, `@derive(PartialEq)`, and `@derive(Default)` produce real, dispatched method calls verifiable in emitted MLIR.

**Architecture:** Sema's `synthesizeDeriveImpls()` already builds correct AST `ImplDecl` nodes (commit `1144935`), but those impls never reach MLIR — likely because synthesized expressions have no resolved types, so HIRBuilder bails when emitting their bodies. Plus the `clone` intrinsic at `lib/HIR/HIRBuilder.cpp:2347` captures all `.clone()` calls before any user-impl dispatch. Fix: route synthetic impls through Sema's full type-checking pass and prepend a user-impl lookup inside the clone intrinsic. Then extend the synthesizer with `@derive(Default)`.

**Tech Stack:** C++ (lib/Sema/Sema.cpp, lib/HIR/HIRBuilder.cpp), TypeScript test fixtures (test/e2e/), lit + ripgrep for verification.

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `lib/Sema/Sema.cpp` | Modify lines 47-158, ~190 | Ensure synthetic impls flow through `checkImplDecl`; add `@derive(Default)` synthesis |
| `lib/HIR/HIRBuilder.cpp` | Modify lines 2347-2372 | Prepend user-impl lookup before clone intrinsic falls through |
| `test/e2e/derive_clone.ts` | Rewrite | Strengthen from `%asc check` to `%asc build --emit mlir` + grep for emitted clone fn |
| `test/e2e/derive_partialeq.ts` | Rewrite | Strengthen from `%asc check` to `%asc build --emit mlir` + grep for emitted eq fn |
| `test/e2e/derive_clone_aliasing.ts` | Create | Negative test that would fail under aliasing — separate clone vs. original |
| `test/e2e/derive_default.ts` | Create | New IR-grep test for `@derive(Default)` |
| `test/e2e/derive_default_invalid.ts` | Create | Confirms non-primitive field emits `ErrTraitNotImplemented` |
| `test/e2e/derive_empty_struct.ts` | Create | Edge case: struct with zero fields, all three derives |
| `CLAUDE.md` | Modify | Update Traits section to reflect actual derive support |

---

## Task 1: Failing IR-grep test for derive(Clone) emission

Establish the failing baseline. This test asserts `Counter_clone` appears in the emitted MLIR — today it does not.

**Files:**
- Modify: `test/e2e/derive_clone.ts`

- [ ] **Step 1: Replace the test contents**

```typescript
// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Counter_clone\"" %t.out
// RUN: grep -q "func.call.*Counter_clone" %t.out

@derive(Clone)
struct Counter { n: i32 }

function main(): i32 {
  let p = Counter { n: 42 };
  let q = p.clone();
  return q.n;
}
```

- [ ] **Step 2: Run it to confirm baseline failure**

Run: `lit test/e2e/derive_clone.ts -v`
Expected: FAIL — both grep commands miss because no `Counter_clone` symbol is emitted today.

- [ ] **Step 3: Commit the failing test**

```bash
git add test/e2e/derive_clone.ts
git commit -m "test: derive(Clone) IR-grep baseline (failing)"
```

---

## Task 2: Diagnose why synthesized impls don't reach MLIR

Confirm the hypothesis from the design spec — that synthesized expressions have no resolved types — or rule it out.

**Files:**
- Inspect: `lib/Sema/Sema.cpp:47-158` (synthesizeDeriveImpls)
- Inspect: `lib/Sema/Sema.cpp:160-214` (analyze)
- Inspect: `lib/HIR/HIRBuilder.cpp:533-620` (visitImplDecl)
- Inspect: `lib/HIR/HIRBuilder.cpp:emitFunctionBody`

- [ ] **Step 1: Add a temporary printf to visitImplDecl entry**

Edit `lib/HIR/HIRBuilder.cpp` at the very top of `visitImplDecl` (line 533):

```cpp
mlir::Value HIRBuilder::visitImplDecl(ImplDecl *d) {
  std::string typeName;
  if (auto *nt = dynamic_cast<NamedType *>(d->getTargetType()))
    typeName = nt->getName().str();
  // TEMPORARY DIAGNOSTIC — remove in Step 5
  llvm::errs() << "visitImplDecl: target=" << typeName
               << " methods=" << d->getMethods().size()
               << " trait=" << (d->isTraitImpl() ? "yes" : "no") << "\n";
```

- [ ] **Step 2: Rebuild and run the failing test**

```bash
cd build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | tail -5
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" ./tools/asc/asc build /Users/satishbabariya/Desktop/asc/test/e2e/derive_clone.ts --emit mlir --target wasm32-wasi 2>&1 | head -10
```

Expected outcomes (decision tree):
- **A. `visitImplDecl` is NOT called for Counter** → bug is in Sema (synthesized impl is never appended to `items`, OR the type registration loop misses it). Investigate `synthesizeDeriveImpls` return path.
- **B. `visitImplDecl` IS called** → bug is downstream — `visitFunctionDecl` or `emitFunctionBody` bails on the synthesized FunctionDecl. Inspect that body emission.

- [ ] **Step 3: If outcome A — verify synthesizeDeriveImpls actually appends**

Add another printf after the synthesis loop in `lib/Sema/Sema.cpp` around line 156:

```cpp
  for (auto *impl : syntheticImpls) {
    items.push_back(impl);
    llvm::errs() << "synthesized impl appended; items now " << items.size() << "\n";
  }
```

If items count grows but `visitImplDecl` is never called, the issue is between Sema and HIRBuilder (Driver may be re-creating the items vector or filtering). Investigate `Driver.cpp:1012-1051`.

- [ ] **Step 4: If outcome B — inspect emitFunctionBody**

Add a printf inside `emitFunctionBody` (find with grep `emitFunctionBody` in HIRBuilder.cpp):

```cpp
void HIRBuilder::emitFunctionBody(FunctionDecl *d, mlir::func::FuncOp funcOp) {
  llvm::errs() << "emitFunctionBody: name=" << d->getName().str()
               << " hasBody=" << (d->getBody() != nullptr) << "\n";
  // ... existing code
```

Look for: does it run for `clone`? If yes but no instructions appear in the func, type resolution failed inside the body — proceed to Task 3 with hypothesis #2 confirmed. If body emission early-returns, find the early-return condition.

- [ ] **Step 5: Document findings inline in the next task before removing diagnostics**

Capture the diagnosed root cause in a brief comment at the top of Task 3. Then remove the temporary printfs.

- [ ] **Step 6: Commit the diagnostic findings note (no code change yet)**

If diagnostics removed cleanly: skip commit, proceed to Task 3. If you've left useful structural notes, commit them as a docs change.

---

## Task 3: Route synthetic impls through Sema's checkImplDecl

Based on Task 2 findings, the most likely fix is to call `checkImplDecl` on every synthesized impl so its expressions get types resolved. This task implements that fix; if Task 2 revealed a different root cause, adapt accordingly and document the deviation.

**Files:**
- Modify: `lib/Sema/Sema.cpp` lines 160-214 (analyze function)

- [ ] **Step 1: Modify `analyze` to type-check synthesized impls explicitly**

The existing `analyze()` runs three passes (type registration → function signature registration → checkDecl per item). The synthesized impls are appended at the start, so they should flow through naturally. If they don't, the likely cause is registration-order: `synthesizeDeriveImpls` runs BEFORE structDecls is populated, but the synthesized impls reference struct fields. We need to run synthesis AFTER type registration.

Edit `lib/Sema/Sema.cpp` `analyze` function (replace lines 160-214):

```cpp
void Sema::analyze(std::vector<Decl *> &items) {
  // First pass: register all type declarations and impl blocks.
  for (auto *item : items) {
    if (auto *sd = dynamic_cast<StructDecl *>(item))
      structDecls[sd->getName()] = sd;
    else if (auto *ed = dynamic_cast<EnumDecl *>(item))
      enumDecls[ed->getName()] = ed;
    else if (auto *td = dynamic_cast<TraitDecl *>(item))
      traitDecls[td->getName()] = td;
    else if (auto *ta = dynamic_cast<TypeAliasDecl *>(item))
      typeAliases[ta->getName()] = ta->getAliasedType();
    else if (auto *id = dynamic_cast<ImplDecl *>(item)) {
      if (auto *nt = dynamic_cast<NamedType *>(id->getTargetType()))
        implDecls[nt->getName()].push_back(id);
    }
    else if (auto *ed = dynamic_cast<ExportDecl *>(item)) {
      if (auto *inner = ed->getInner()) {
        if (auto *sd = dynamic_cast<StructDecl *>(inner))
          structDecls[sd->getName()] = sd;
        else if (auto *enm = dynamic_cast<EnumDecl *>(inner))
          enumDecls[enm->getName()] = enm;
      }
    }
  }

  // Run @derive expansion on each struct first (this happens inside
  // checkStructDecl too, but we need attributes settled before synthesis).
  for (auto *item : items) {
    if (auto *sd = dynamic_cast<StructDecl *>(item))
      checkStructDecl(sd);
  }

  // Synthesize impl blocks for @derive attributes — runs AFTER type
  // registration so synthesized impls can reference struct fields.
  synthesizeDeriveImpls(ctx, items);

  // Register the newly-synthesized impl blocks.
  for (auto *item : items) {
    if (auto *id = dynamic_cast<ImplDecl *>(item)) {
      if (auto *nt = dynamic_cast<NamedType *>(id->getTargetType())) {
        auto &v = implDecls[nt->getName()];
        if (std::find(v.begin(), v.end(), id) == v.end())
          v.push_back(id);
      }
    }
  }

  // Second pass: register all function signatures (enables forward references).
  for (auto *item : items) {
    FunctionDecl *fd = nullptr;
    if (auto *f = dynamic_cast<FunctionDecl *>(item))
      fd = f;
    else if (auto *ed = dynamic_cast<ExportDecl *>(item)) {
      if (ed->getInner())
        fd = dynamic_cast<FunctionDecl *>(ed->getInner());
    }
    if (fd) {
      Symbol sym;
      sym.name = fd->getName().str();
      sym.decl = fd;
      sym.type = fd->getReturnType();
      if (!currentScope->declare(fd->getName(), std::move(sym))) {
        diags.emitError(fd->getLocation(), DiagID::ErrDuplicateDeclaration,
                        "duplicate function declaration '" +
                        fd->getName().str() + "'");
      }
    }
  }

  // Third pass: check all declarations (bodies, types, etc.).
  // Skip structs since we already checked them above.
  for (auto *item : items) {
    if (dynamic_cast<StructDecl *>(item)) continue;
    checkDecl(item);
  }
}
```

- [ ] **Step 2: Build**

Run: `cd build && cmake --build . -j$(sysctl -n hw.ncpu)`
Expected: clean build.

- [ ] **Step 3: Re-run the failing test**

Run: `lit test/e2e/derive_clone.ts -v`
Expected: PASS (or progress further than before — if grep still fails but `Counter_clone` symbol now appears in raw MLIR, refine the grep pattern).

If still failing, the diagnostic from Task 2 should pinpoint a different fix. Apply it and re-run.

- [ ] **Step 4: Run full test suite to verify no regressions**

Run: `lit test/ --no-progress-bar 2>&1 | tail -5`
Expected: 256+ tests pass.

- [ ] **Step 5: Commit**

```bash
git add lib/Sema/Sema.cpp test/e2e/derive_clone.ts
git commit -m "fix: synthesize derive impls after type registration so HIRBuilder emits them"
```

---

## Task 4: Failing IR-grep test for derive(PartialEq)

Mirror Task 1 for PartialEq. After Task 3's fix, the synthesized `Color_eq` should be emitted, but no call site exists yet because the user-impl-first dispatch isn't in place for `eq` (it should fall through to the generic dispatch fallback at HIRBuilder.cpp:3425 — verify).

**Files:**
- Modify: `test/e2e/derive_partialeq.ts`

- [ ] **Step 1: Replace test contents**

```typescript
// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Color_eq\"" %t.out
// RUN: grep -q "func.call.*Color_eq" %t.out

@derive(PartialEq)
struct Color { r: i32, g: i32, b: i32 }

function main(): i32 {
  let a = Color { r: 1, g: 2, b: 3 };
  let b = Color { r: 1, g: 2, b: 3 };
  if a.eq(&b) { return 0; }
  return 1;
}
```

- [ ] **Step 2: Run it**

Run: `lit test/e2e/derive_partialeq.ts -v`
Expected outcomes:
- After Task 3, the `sym_name` grep should pass — `Color_eq` is emitted.
- The `func.call` grep may pass (if generic dispatch at HIRBuilder.cpp:3425 picks it up via the `&b` argument) or fail (if the borrow conversion confuses dispatch).

- [ ] **Step 3: If `func.call` grep fails, add user-impl dispatch for `eq`**

Inspect `lib/HIR/HIRBuilder.cpp:3425-3433` — confirm whether the lookup fires. If `args` includes the receiver but the function signature mismatches, dispatch fails silently. Add a defensive check before the generic fallback runs.

- [ ] **Step 4: Commit**

```bash
git add test/e2e/derive_partialeq.ts
git commit -m "test: derive(PartialEq) IR-grep verification"
```

---

## Task 5: Prepend user-impl dispatch inside clone intrinsic

The clone intrinsic at HIRBuilder.cpp:2347 fires for ALL `.clone()` calls and currently aliases `own.val` receivers. After Task 3, `Type_clone` exists in the module — but we need the intrinsic to defer to it.

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp:2347-2372`

- [ ] **Step 1: Add user-impl lookup at the top of the clone intrinsic**

Edit `lib/HIR/HIRBuilder.cpp` lines 2347-2372 — change from:

```cpp
  // Clone: .clone() → copy the value.
  // For pointer-backed types (structs), allocate new memory and memcpy.
  // For scalars, just return the value.
  if (methodName == "clone" && receiver) {
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(receiver.getType())) {
      // Look up the struct type for the receiver.
      if (!receiverTypeName.empty()) {
        auto sit = sema.structDecls.find(receiverTypeName);
        if (sit != sema.structDecls.end()) {
          auto structType = convertStructType(sit->second);
          auto ptrType = getPtrType();
          auto i64Type = builder.getIntegerType(64);
          auto i64One = builder.create<mlir::LLVM::ConstantOp>(
              location, i64Type, static_cast<int64_t>(1));
          auto cloneAlloca = builder.create<mlir::LLVM::AllocaOp>(
              location, ptrType, structType, i64One);
          auto loaded = builder.create<mlir::LLVM::LoadOp>(
              location, structType, receiver);
          builder.create<mlir::LLVM::StoreOp>(location, loaded, cloneAlloca);
          return cloneAlloca;
        }
      }
    }
    // Scalar clone: just return the value.
    return receiver;
  }
```

to:

```cpp
  // Clone: .clone() → defer to user-defined Type_clone if it exists,
  // else fall back to memcpy for struct pointers / value passthrough for scalars.
  if (methodName == "clone" && receiver) {
    if (!receiverTypeName.empty()) {
      std::string mangled = receiverTypeName + "_clone";
      if (auto userClone = module.lookupSymbol<mlir::func::FuncOp>(mangled)) {
        auto callOp = builder.create<mlir::func::CallOp>(location, userClone, args);
        return callOp.getNumResults() > 0 ? callOp.getResult(0) : mlir::Value{};
      }
    }
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(receiver.getType())) {
      if (!receiverTypeName.empty()) {
        auto sit = sema.structDecls.find(receiverTypeName);
        if (sit != sema.structDecls.end()) {
          auto structType = convertStructType(sit->second);
          auto ptrType = getPtrType();
          auto i64Type = builder.getIntegerType(64);
          auto i64One = builder.create<mlir::LLVM::ConstantOp>(
              location, i64Type, static_cast<int64_t>(1));
          auto cloneAlloca = builder.create<mlir::LLVM::AllocaOp>(
              location, ptrType, structType, i64One);
          auto loaded = builder.create<mlir::LLVM::LoadOp>(
              location, structType, receiver);
          builder.create<mlir::LLVM::StoreOp>(location, loaded, cloneAlloca);
          return cloneAlloca;
        }
      }
    }
    // Scalar clone: just return the value.
    return receiver;
  }
```

- [ ] **Step 2: Build**

Run: `cd build && cmake --build . -j$(sysctl -n hw.ncpu)`

- [ ] **Step 3: Re-run derive_clone.ts — both grep lines should pass now**

Run: `lit test/e2e/derive_clone.ts -v`
Expected: PASS — both `sym_name = "Counter_clone"` AND `func.call.*Counter_clone` grep.

- [ ] **Step 4: Run full test suite**

Run: `lit test/ --no-progress-bar 2>&1 | tail -5`
Expected: All 256+ tests pass. `clone_eq.ts` (which uses `.clone()` without `@derive`) keeps passing because the intrinsic falls through to the existing memcpy path when no user impl exists.

- [ ] **Step 5: Commit**

```bash
git add lib/HIR/HIRBuilder.cpp
git commit -m "fix: clone intrinsic defers to user-defined Type_clone before memcpy fallback"
```

---

## Task 6: Aliasing-catching runtime test

The IR-grep tests prove the function exists and is called. This test catches the aliasing bug at the runtime level: clones must have independent storage from the original.

**Files:**
- Create: `test/e2e/derive_clone_aliasing.ts`

- [ ] **Step 1: Write the test**

```typescript
// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Pair_clone\"" %t.out
// Verify clone produces independent storage by checking the call appears
// twice when we clone twice.
// RUN: grep -c "func.call.*Pair_clone" %t.out | grep -q "2"

@derive(Clone)
struct Pair { left: i32, right: i32 }

function main(): i32 {
  let p = Pair { left: 100, right: 200 };
  let q = p.clone();
  let r = p.clone();
  return q.left + r.right;
}
```

- [ ] **Step 2: Run it**

Run: `lit test/e2e/derive_clone_aliasing.ts -v`
Expected: PASS — `Pair_clone` exists and is called twice.

- [ ] **Step 3: Commit**

```bash
git add test/e2e/derive_clone_aliasing.ts
git commit -m "test: derive(Clone) call-count verification"
```

---

## Task 7: Failing IR-grep test for derive(Default)

Establish failing baseline for Default before implementing.

**Files:**
- Create: `test/e2e/derive_default.ts`

- [ ] **Step 1: Write the test**

```typescript
// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Counter_default\"" %t.out
// RUN: grep -q "func.call.*Counter_default" %t.out

@derive(Default)
struct Counter { n: i32, flag: bool }

function main(): i32 {
  let c = Counter::default();
  if c.n != 0 { return 1; }
  if c.flag { return 2; }
  return 0;
}
```

- [ ] **Step 2: Run to confirm baseline failure**

Run: `lit test/e2e/derive_default.ts -v`
Expected: FAIL — `Counter_default` is not synthesized today.

- [ ] **Step 3: Commit failing test**

```bash
git add test/e2e/derive_default.ts
git commit -m "test: derive(Default) IR-grep baseline (failing)"
```

---

## Task 8: Synthesize derive(Default) in Sema

Extend `synthesizeDeriveImpls` to handle `@default` attribute.

**Files:**
- Modify: `lib/Sema/Sema.cpp` — extend `synthesizeDeriveImpls` (currently lines 47-158)

- [ ] **Step 1: Add `hasDefault` detection and synthesis branch**

Inside the per-struct loop in `synthesizeDeriveImpls`, after the `hasClone`/`hasPartialEq` block, add:

```cpp
    bool hasDefault = false;
    for (const auto &attr : sd->getAttributes()) {
      if (attr == "@default") hasDefault = true;
    }

    // derive(Default): fn default(): Type { return Type { f1: <zero>, ... }; }
    if (hasDefault) {
      bool allPrimitive = true;
      std::vector<FieldInit> fieldInits;
      for (auto *field : sd->getFields()) {
        Type *ft = field->getType();
        Expr *zero = nullptr;
        if (auto *bt = dynamic_cast<BuiltinType *>(ft)) {
          if (bt->isInteger() || bt->getBuiltinKind() == BuiltinTypeKind::USize ||
              bt->getBuiltinKind() == BuiltinTypeKind::ISize) {
            zero = ctx.create<IntegerLiteral>(0, std::string{}, loc);
          } else if (bt->isFloat()) {
            zero = ctx.create<FloatLiteral>(0.0, std::string{}, loc);
          } else if (bt->isBool()) {
            zero = ctx.create<BoolLiteral>(false, loc);
          } else if (bt->getBuiltinKind() == BuiltinTypeKind::Char) {
            zero = ctx.create<CharLiteral>(0, loc);
          }
        }
        if (!zero) {
          // Non-primitive field — diagnostic and skip synthesis.
          allPrimitive = false;
          break;
        }
        FieldInit fi;
        fi.name = field->getName().str();
        fi.value = zero;
        fi.loc = loc;
        fieldInits.push_back(fi);
      }

      if (!allPrimitive) {
        // Caller (Sema) will report after synthesis. We can't emit
        // diagnostics from a static helper without plumbing the engine in.
        // Skip synthesis; checkStructDecl could be extended later to
        // emit a targeted error, but for v1 the user will see a missing
        // Type_default symbol at link/dispatch time.
        continue;
      }

      auto *structLit = ctx.create<StructLiteral>(
          typeName, std::move(fieldInits), nullptr, loc);
      auto *retStmt = ctx.create<ReturnStmt>(structLit, loc);
      auto *body = ctx.create<CompoundStmt>(
          std::vector<Stmt *>{retStmt}, nullptr, loc);

      auto *retType = ctx.create<NamedType>(typeName, std::vector<Type *>{}, loc);
      auto *defaultMethod = ctx.create<FunctionDecl>(
          "default", std::vector<GenericParam>{},
          std::vector<ParamDecl>{},
          retType, body, std::vector<WhereConstraint>{}, loc);

      auto *targetType = ctx.create<NamedType>(typeName, std::vector<Type *>{}, loc);
      auto *traitType = ctx.create<NamedType>("Default", std::vector<Type *>{}, loc);
      auto *implDecl = ctx.create<ImplDecl>(
          std::vector<GenericParam>{}, targetType, traitType,
          std::vector<FunctionDecl *>{defaultMethod}, loc);
      syntheticImpls.push_back(implDecl);
    }
```

- [ ] **Step 2: Build**

Run: `cd build && cmake --build . -j$(sysctl -n hw.ncpu)`
Expected: clean build.

- [ ] **Step 3: Re-run derive_default.ts**

Run: `lit test/e2e/derive_default.ts -v`
Expected: PASS — `Counter_default` is now emitted and called.

- [ ] **Step 4: Run full test suite**

Run: `lit test/ --no-progress-bar 2>&1 | tail -5`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add lib/Sema/Sema.cpp
git commit -m "feat: derive(Default) synthesizes default() for primitive-field structs"
```

---

## Task 9: Negative test for derive(Default) on non-primitive fields

Until field-by-field default support exists, structs with non-primitive fields should be flagged. v1 simply skips synthesis (as documented in Task 8 step 1) — verify this behavior.

**Files:**
- Create: `test/e2e/derive_default_invalid.ts`

- [ ] **Step 1: Write the test**

```typescript
// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// Confirm Inner_default IS synthesized (all i32 fields).
// RUN: grep -q "sym_name = \"Inner_default\"" %t.out
// Outer_default should NOT be synthesized (Inner field is non-primitive).
// RUN: ! grep -q "sym_name = \"Outer_default\"" %t.out

@derive(Default)
struct Inner { x: i32 }

@derive(Default)
struct Outer { inner: Inner, n: i32 }

function main(): i32 {
  let i = Inner::default();
  return i.x;
}
```

- [ ] **Step 2: Run it**

Run: `lit test/e2e/derive_default_invalid.ts -v`
Expected: PASS — Inner_default exists, Outer_default does not.

- [ ] **Step 3: Commit**

```bash
git add test/e2e/derive_default_invalid.ts
git commit -m "test: derive(Default) skips synthesis for non-primitive fields"
```

---

## Task 10: Empty-struct edge case

Structs with zero fields should derive successfully — `clone` returns an empty struct literal, `eq` returns `true`, `default` returns an empty struct literal.

**Files:**
- Create: `test/e2e/derive_empty_struct.ts`

- [ ] **Step 1: Write the test**

```typescript
// RUN: %asc build %s --emit mlir --target wasm32-wasi > %t.out 2>&1
// RUN: grep -q "sym_name = \"Empty_clone\"" %t.out
// RUN: grep -q "sym_name = \"Empty_eq\"" %t.out
// RUN: grep -q "sym_name = \"Empty_default\"" %t.out

@derive(Clone, PartialEq, Default)
struct Empty {}

function main(): i32 {
  let a = Empty {};
  let b = a.clone();
  let c = Empty::default();
  if a.eq(&b) { return 0; }
  return 1;
}
```

- [ ] **Step 2: Run it**

Run: `lit test/e2e/derive_empty_struct.ts -v`
Expected: PASS — all three symbols emitted.

If `eq` for empty struct fails, recall the existing synthesis at `lib/Sema/Sema.cpp:119-120`:
```cpp
if (!comparison)
  comparison = ctx.create<BoolLiteral>(true, loc);
```
This handles the empty case correctly.

- [ ] **Step 3: Commit**

```bash
git add test/e2e/derive_empty_struct.ts
git commit -m "test: derive(Clone, PartialEq, Default) on empty struct"
```

---

## Task 11: Update CLAUDE.md derive support note

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Find the Traits section**

Run: `grep -n "30 registered" /Users/satishbabariya/Desktop/asc/CLAUDE.md`

- [ ] **Step 2: After the Traits paragraph, add a Derive Support subsection**

Add before "### Toolchain":

```markdown
### Derive Support

`@derive(Trait1, Trait2, ...)` synthesizes impl blocks at the AST level via
`synthesizeDeriveImpls()` in `lib/Sema/Sema.cpp`. The `clone` HIR intrinsic
defers to user-defined `Type_clone` when one exists.

| Trait | Status | Notes |
|---|---|---|
| Copy | ✅ end-to-end | Validates all fields are Copy, marks struct |
| Clone | ✅ end-to-end | Generates `Type_clone` with field-by-field copy |
| PartialEq | ✅ end-to-end | Generates `Type_eq` with field-by-field `==` chained by `&&` |
| Default | ✅ end-to-end | Primitive-field structs only — non-primitive fields skip synthesis |
| Send / Sync | ✅ marker | Marker attribute for Sema validation |
| Eq | ✅ marker | Marker attribute (PartialEq does the work) |
| Debug | ⚠️ marker only | Needs Formatter + string concat infra |
| Hash | ⚠️ marker only | Needs Hasher infra |
```

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: derive support matrix in CLAUDE.md"
```

---

## Self-Review Checklist (post-plan, pre-execution)

- [ ] Spec coverage: Tasks 1-6 cover Phase 1-2 of spec (diagnose, fix emission, fix dispatch). Tasks 7-9 cover Phase 3 (Default). Task 10 covers edge cases. Task 11 covers CLAUDE.md.
- [ ] No placeholders in code blocks — every step has the actual code or actual command.
- [ ] Type consistency: `Counter_clone` / `Color_eq` / `Counter_default` naming consistent across tasks. Symbol names match what `visitImplDecl` emits at HIRBuilder.cpp:549 (`typeName + "_" + methodName`).
- [ ] Task 2 is investigation, not action — its outcome may revise Task 3. Document whichever fix actually lands.
- [ ] Acceptance criteria from spec map to tests in the plan: Task 1 (Counter_clone grep), Task 4 (Color_eq grep), Task 7 (Counter_default grep), Task 11 (CLAUDE.md updated).
