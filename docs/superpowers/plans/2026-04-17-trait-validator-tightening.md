# Trait Validator Tightening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tighten `Sema::checkImplDecl` to reject `impl <UnknownTrait> for T`, reject unknown trait names in generic bounds, and enforce exact method-signature match between impl blocks and their registered trait declaration (after `Self → ConcreteType` substitution).

**Architecture:** Two new `DiagID` values and two new helper functions in `lib/Sema/SemaDecl.cpp`. Level 1 adds an `else` branch to the existing trait-lookup block and a new `validateGenericBounds` helper called from five decl-check sites. Level 2 adds `substSelf` (type-walking substitutor) and `signaturesMatchAfterSelfSub` (structural comparator) and invokes the latter inside the `impl Trait for T` check.

**Tech Stack:** C++ (LLVM 18 / MLIR 18), CMake, lit. Same surface as PR #40.

---

## File Structure

**Modified files:**
- `include/asc/Basic/DiagnosticIDs.h` — add 2 new DiagIDs (`ErrUnknownTrait = 222`, `ErrTraitSignatureMismatch = 223`).
- `lib/Sema/SemaDecl.cpp` — extend `checkImplDecl` (add `else` branch, add signature-match loop); add file-static helpers `substSelf` and `signaturesMatchAfterSelfSub`; add `validateGenericBounds` helper and call it from `checkFunctionDecl`, `checkStructDecl`, `checkEnumDecl`, `checkTraitDecl`, `checkImplDecl`.
- `test/e2e/operator_add_impl.ts` — fix signature to match registered Add trait (known break, confirmed during brainstorm).
- Any other existing impls surfaced by Task 4's "find and fix" cycle.

**New files (all under `test/e2e/`):**
- `trait_unknown_name_impl.ts` — negative test: `impl BogusTrait for T`
- `trait_unknown_name_bound.ts` — negative test: `fn f<T: BogusTrait>(x: T)`
- `trait_signature_mismatch_param.ts` — negative test: `impl Rem for T` using `ref<Self>` instead of `own<Self>`
- `trait_signature_mismatch_return.ts` — negative test: `impl Rem for T` returning `i32` instead of `own<T>`
- `trait_signature_self_sub_positive.ts` — positive test: `impl Clone for Foo` using `fn clone(ref<Self>): own<Foo>`

## Pre-flight: baseline and branch

```bash
cd /Users/satishbabariya/Desktop/asc
git checkout main && git pull
git checkout -b trait-validator-tightening
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -3
```

Expected: 270 passing (commit a812443 post-merge).

---

## Task 1: Add new DiagIDs

**Files:**
- Modify: `include/asc/Basic/DiagnosticIDs.h`

- [ ] **Step 1: Add two enum entries**

Open `include/asc/Basic/DiagnosticIDs.h`. Locate the line `ErrExpectedClosingBracket = 221,` (line ~61). Immediately after it and before the `// Notes` block, insert:

```cpp
  ErrUnknownTrait = 222,
  ErrTraitSignatureMismatch = 223,
```

No entries needed in the `getDiagCode` switch — entries in the 200-range do not have E-code strings per the file's current convention (`ErrTraitNotImplemented = 212` has no E-code either).

- [ ] **Step 2: Rebuild to confirm headers compile**

Run: `cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3`
Expected: build succeeds with no errors.

- [ ] **Step 3: Commit**

```bash
git add include/asc/Basic/DiagnosticIDs.h
git commit -m "$(cat <<'EOF'
sema: add DiagIDs for trait validator tightening

Adds ErrUnknownTrait (222) and ErrTraitSignatureMismatch (223). Not yet
emitted; wiring in follow-up commits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Level 1a — reject unknown trait in impl block

**Files:**
- Create: `test/e2e/trait_unknown_name_impl.ts`
- Modify: `lib/Sema/SemaDecl.cpp` (around line 189)

- [ ] **Step 1: Write the failing negative test**

Create `test/e2e/trait_unknown_name_impl.ts`:

```typescript
// RUN: not %asc check %s 2>&1 | grep -q "unknown trait"
// Test: `impl <UnknownTrait> for T` is rejected.

struct Foo { v: i32 }

impl BogusTraitThatDoesNotExist for Foo {
  fn whatever(self: ref<Self>): i32 { return 0; }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/trait_unknown_name_impl.ts -v 2>&1 | tail -10`
Expected: FAIL — current validator accepts unknown trait names, so `asc check` exits 0 and the test expects nonzero exit.

- [ ] **Step 3: Add the else branch in `checkImplDecl`**

Open `lib/Sema/SemaDecl.cpp`. Locate the `if (d->isTraitImpl()) { ... }` block starting around line 186. Inside it, find:

```cpp
    if (auto *namedType = dynamic_cast<NamedType *>(d->getTraitType())) {
      auto it = traitDecls.find(namedType->getName());
      if (it != traitDecls.end()) {
        TraitDecl *trait = it->second;
        for (const auto &item : trait->getItems()) {
          // ... existing method-presence check ...
        }
      }
    }
```

Change it to:

```cpp
    if (auto *namedType = dynamic_cast<NamedType *>(d->getTraitType())) {
      auto it = traitDecls.find(namedType->getName());
      if (it != traitDecls.end()) {
        TraitDecl *trait = it->second;
        for (const auto &item : trait->getItems()) {
          // ... existing method-presence check ...
        }
      } else {
        diags.emitError(
            d->getLocation(), DiagID::ErrUnknownTrait,
            "unknown trait '" + namedType->getName().str() + "'");
      }
    }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/trait_unknown_name_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES (check failed as expected, grep matched "unknown trait").

- [ ] **Step 5: Run full suite to confirm no regressions**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -5`
Expected: `Passed: 271` (270 baseline + 1 new). Zero failures.

- [ ] **Step 6: Commit**

```bash
git add test/e2e/trait_unknown_name_impl.ts lib/Sema/SemaDecl.cpp
git commit -m "$(cat <<'EOF'
sema: reject unknown trait in impl block

`impl <UnknownTrait> for T` is now a compile error. Previously silently
accepted.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Level 1b — reject unknown trait in generic bound

**Files:**
- Create: `test/e2e/trait_unknown_name_bound.ts`
- Modify: `lib/Sema/SemaDecl.cpp` (new helper `validateGenericBounds` + 5 call sites)

- [ ] **Step 1: Write the failing negative test**

Create `test/e2e/trait_unknown_name_bound.ts`:

```typescript
// RUN: not %asc check %s 2>&1 | grep -q "unknown trait"
// Test: generic bound with an unknown trait is rejected.

function process<T: BogusTraitThatDoesNotExist>(x: T): i32 {
  return 0;
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/trait_unknown_name_bound.ts -v 2>&1 | tail -10`
Expected: FAIL — current validator doesn't check bounds at all.

- [ ] **Step 3: Add the `validateGenericBoundsImpl` helper**

Open `lib/Sema/SemaDecl.cpp`. Near the top of the file (before `checkFunctionDecl`), add as a file-static free function — no `Sema.h` modification required:

```cpp
static void validateGenericBoundsImpl(
    const std::vector<GenericParam> &params,
    const std::unordered_map<std::string, TraitDecl *> &traitDecls,
    DiagnosticEngine &diags,
    SourceLocation loc) {
  for (const auto &p : params) {
    for (const auto &bound : p.bounds) {
      // bound is a Type * pointing at a NamedType naming the trait
      if (auto *nt = dynamic_cast<NamedType *>(bound)) {
        if (traitDecls.find(nt->getName().str()) == traitDecls.end()) {
          diags.emitError(
              loc, DiagID::ErrUnknownTrait,
              "unknown trait '" + nt->getName().str() + "' in generic bound");
        }
      }
    }
  }
}
```

**Type/include check:** if `GenericParam.bounds` is not `std::vector<Type *>` (for example, a custom `TypeBound` struct), adjust the inner loop's cast target accordingly. A quick grep `grep -n "struct GenericParam\|class GenericParam" include/asc/AST/` will show the exact field type.

- [ ] **Step 4: Call from every decl-check entry point**

First, enumerate the decl-check methods: run `grep -n "^void Sema::check" lib/Sema/SemaDecl.cpp`. Expected output lists `checkFunctionDecl`, `checkStructDecl`, `checkEnumDecl`, `checkTraitDecl`, `checkImplDecl` (possibly others).

At the top of each such method body, insert:

```cpp
  validateGenericBoundsImpl(
      d->getGenericParams(), traitDecls, diags, d->getLocation());
```

If a particular decl type doesn't have `getGenericParams()` (e.g., some don't carry generics), skip that one — but `FunctionDecl`, `StructDecl`, `EnumDecl`, `TraitDecl`, and `ImplDecl` all carry generics per the constructors in `lib/Sema/Builtins.cpp`.

- [ ] **Step 5: Rebuild and run both Level 1 tests**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/trait_unknown_name_impl.ts test/e2e/trait_unknown_name_bound.ts -v 2>&1 | tail -10
```

Expected: build succeeds, both tests PASS.

- [ ] **Step 6: Run full suite**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -5`
Expected: `Passed: 272` (270 baseline + 2 new from Tasks 2 and 3). If any existing tests now fail, the failures are existing code with unknown-trait names in bounds — fix them as part of this task before committing.

- [ ] **Step 7: Commit**

```bash
git add test/e2e/trait_unknown_name_bound.ts lib/Sema/SemaDecl.cpp
git commit -m "$(cat <<'EOF'
sema: reject unknown trait in generic bounds

Generic bounds referencing unknown trait names now emit ErrUnknownTrait.
Applies at function, struct, enum, trait, and impl declaration sites.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Level 2 — signature match after Self substitution

This task lands the core Level 2 check AND fixes the existing impls that break under it. It's larger than earlier tasks — expect one or two follow-up commits to fix broken impls.

**Files:**
- Create: `test/e2e/trait_signature_mismatch_param.ts`
- Modify: `lib/Sema/SemaDecl.cpp` (add `substSelf`, `signaturesMatchAfterSelfSub`; integrate into `checkImplDecl`)
- Modify: existing impl files that fail after the check lands (at minimum `test/e2e/operator_add_impl.ts`; others will be surfaced by the failing-test output in Step 9)

- [ ] **Step 1: Write the failing negative test**

Create `test/e2e/trait_signature_mismatch_param.ts`:

```typescript
// RUN: not %asc check %s 2>&1 | grep -q "signature does not match"
// Test: impl method using ref<Self> where trait declares own<Self> is rejected.

struct N { v: i32 }

impl Rem for N {
  fn rem(self: ref<Self>, rhs: ref<Self>): own<N> {
    return N { v: self.v % rhs.v };
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/trait_signature_mismatch_param.ts -v 2>&1 | tail -10`
Expected: FAIL — current validator does not compare signatures.

- [ ] **Step 3: Add `substSelf` helper**

In `lib/Sema/SemaDecl.cpp`, near the top of the file (before any use), add a file-static helper:

```cpp
// Returns a new Type * that is `original` with every NamedType("Self")
// replaced by `concrete`. Preserves wrappers (OwnType/RefType/RefMutType).
// Allocates new Type * nodes via ctx.
static Type *substSelf(ASTContext &ctx, Type *original, Type *concrete) {
  if (!original) return nullptr;
  if (auto *nt = dynamic_cast<NamedType *>(original)) {
    if (nt->getName() == "Self") return concrete;
    return original;
  }
  if (auto *ot = dynamic_cast<OwnType *>(original)) {
    Type *inner = substSelf(ctx, ot->getInner(), concrete);
    if (inner == ot->getInner()) return original;
    return ctx.create<OwnType>(inner, ot->getLocation());
  }
  if (auto *rt = dynamic_cast<RefType *>(original)) {
    Type *inner = substSelf(ctx, rt->getInner(), concrete);
    if (inner == rt->getInner()) return original;
    return ctx.create<RefType>(inner, rt->getLocation());
  }
  if (auto *rmt = dynamic_cast<RefMutType *>(original)) {
    Type *inner = substSelf(ctx, rmt->getInner(), concrete);
    if (inner == rmt->getInner()) return original;
    return ctx.create<RefMutType>(inner, rmt->getLocation());
  }
  // Other wrapper kinds (slice/array/tuple/function): for now, return as-is.
  // Self inside these is rare in trait signatures; extend if tests surface it.
  return original;
}
```

- [ ] **Step 4: Add `typeEquals` helper**

In `lib/Sema/SemaDecl.cpp`, near `substSelf`, add:

```cpp
// Structural type equality. Pointer identity first (bump-allocated types
// are often canonicalized), then recursive kind+inner comparison.
static bool typeEquals(Type *a, Type *b) {
  if (a == b) return true;
  if (!a || !b) return false;

  if (auto *na = dynamic_cast<NamedType *>(a)) {
    auto *nb = dynamic_cast<NamedType *>(b);
    return nb && na->getName() == nb->getName();
  }
  if (auto *oa = dynamic_cast<OwnType *>(a)) {
    auto *ob = dynamic_cast<OwnType *>(b);
    return ob && typeEquals(oa->getInner(), ob->getInner());
  }
  if (auto *ra = dynamic_cast<RefType *>(a)) {
    auto *rb = dynamic_cast<RefType *>(b);
    return rb && typeEquals(ra->getInner(), rb->getInner());
  }
  if (auto *rma = dynamic_cast<RefMutType *>(a)) {
    auto *rmb = dynamic_cast<RefMutType *>(b);
    return rmb && typeEquals(rma->getInner(), rmb->getInner());
  }
  // Other type kinds not part of trait signatures we register.
  return false;
}
```

- [ ] **Step 5: Add `signaturesMatchAfterSelfSub` helper**

In `lib/Sema/SemaDecl.cpp`, near the two helpers above, add:

```cpp
static bool signaturesMatchAfterSelfSub(
    ASTContext &ctx,
    FunctionDecl *traitMethod,
    FunctionDecl *implMethod,
    Type *concreteSelf) {
  const auto &tp = traitMethod->getParams();
  const auto &ip = implMethod->getParams();
  if (tp.size() != ip.size()) return false;
  for (size_t i = 0; i < tp.size(); ++i) {
    Type *expected = substSelf(ctx, tp[i].type, concreteSelf);
    if (!typeEquals(expected, ip[i].type)) return false;
  }
  Type *expectedRet = substSelf(ctx, traitMethod->getReturnType(), concreteSelf);
  if (!typeEquals(expectedRet, implMethod->getReturnType())) return false;
  return true;
}
```

- [ ] **Step 6: Integrate the check into `checkImplDecl`**

Inside the existing `if (it != traitDecls.end())` block in `checkImplDecl`, **after** the existing method-presence check but **before** the block's closing brace, add a signature-match loop:

```cpp
        // Signature compatibility check.
        for (const auto &item : trait->getItems()) {
          if (!item.method || item.method->getBody())
            continue;
          // Find the impl method by name.
          FunctionDecl *implMethod = nullptr;
          for (auto *m : d->getMethods()) {
            if (m->getName() == item.method->getName()) {
              implMethod = m;
              break;
            }
          }
          if (!implMethod) continue; // missing-method error already emitted above
          if (!signaturesMatchAfterSelfSub(
                  ctx, item.method, implMethod, d->getTargetType())) {
            diags.emitError(
                implMethod->getLocation(),
                DiagID::ErrTraitSignatureMismatch,
                "method '" + implMethod->getName().str() +
                "' signature does not match trait '" +
                namedType->getName().str() + "'");
          }
        }
```

- [ ] **Step 7: Rebuild**

Run: `cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3`
Expected: build succeeds.

- [ ] **Step 8: Verify the new negative test passes**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/trait_signature_mismatch_param.ts -v 2>&1 | tail -5`
Expected: PASS (check failed as expected).

- [ ] **Step 9: Run full suite to find broken existing impls**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | grep -E "FAIL|Failed"`

Record every failing test. At minimum, expect `test/e2e/operator_add_impl.ts` to fail. Others likely but empirical.

- [ ] **Step 10: Fix each broken impl**

For each failing test, open the file and update the impl signature to match its registered trait. Known fix for `test/e2e/operator_add_impl.ts`:

Before (lines 6-8):
```typescript
impl Add for Counter {
  function add(self: ref<Counter>, other: ref<Counter>): Counter {
    return Counter { val: self.val + other.val };
  }
}
```

After:
```typescript
impl Add for Counter {
  function add(self: own<Self>, rhs: own<Self>): own<Counter> {
    return Counter { val: self.val + rhs.val };
  }
}
```

Rationale: registered Add trait is `fn add(own<Self>, own<Self>): Self`. With `Self := Counter`, expected signature is `fn add(own<Counter>, own<Counter>): Counter`. Returning `own<Counter>` is the user-written form that compiles to the same type.

For each additional failing file, apply the same pattern: find the registered trait signature in `lib/Sema/Builtins.cpp` and update the impl to match after Self substitution.

- [ ] **Step 11: Re-run full suite until clean**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -5`
Expected after fixes: `Passed: 273` (270 baseline + 3 new tests from Tasks 2, 3, 4). Zero failures.

- [ ] **Step 12: Commit**

```bash
git add test/e2e/trait_signature_mismatch_param.ts lib/Sema/SemaDecl.cpp
# Add each fixed impl file:
git add test/e2e/operator_add_impl.ts
# ... any others from Step 10 ...
git commit -m "$(cat <<'EOF'
sema: enforce trait method signature match

impl methods must match the registered trait declaration exactly after
Self -> ConcreteType substitution. Parameter types and return type compared
structurally; no variance or subtyping.

Fixes existing impls that relied on the loose validator:
- test/e2e/operator_add_impl.ts (ref<Counter> -> own<Self>)
- (others discovered by running the suite)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Edit the commit message to list the actual files fixed in Step 10.

---

## Task 5: Level 2 negative test — return type mismatch

Validates that return-type mismatches are also caught. No implementation work — Task 4's `signaturesMatchAfterSelfSub` already compares return types. This task extends coverage.

**Files:**
- Create: `test/e2e/trait_signature_mismatch_return.ts`

- [ ] **Step 1: Write the negative test**

Create `test/e2e/trait_signature_mismatch_return.ts`:

```typescript
// RUN: not %asc check %s 2>&1 | grep -q "signature does not match"
// Test: impl method with wrong return type is rejected.

struct N { v: i32 }

impl Rem for N {
  fn rem(self: own<Self>, rhs: own<Self>): i32 {
    return self.v % rhs.v;
  }
}

function main(): i32 { return 0; }
```

Registered Rem return is `Self` (→ `own<N>` after substitution? — actually `Self` substitutes to `N`). The impl returns `i32`. Mismatch on return type.

- [ ] **Step 2: Run the test**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/trait_signature_mismatch_return.ts -v 2>&1 | tail -5`
Expected: PASS (check failed as expected because return types differ).

- [ ] **Step 3: Run full suite**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -3`
Expected: `Passed: 274`.

- [ ] **Step 4: Commit**

```bash
git add test/e2e/trait_signature_mismatch_return.ts
git commit -m "$(cat <<'EOF'
test(sema): guard trait signature return-type mismatch

Extends Task 4 coverage with a return-type-specific regression test.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Level 2 positive test — Self substitution accepts the common idiom

Positive regression guard: the `impl Clone for Foo { fn clone(ref<Self>): own<Foo> }` idiom used throughout std/ must continue to typecheck. This protects against over-tightening `substSelf` or `typeEquals` in future refactors.

**Files:**
- Create: `test/e2e/trait_signature_self_sub_positive.ts`

- [ ] **Step 1: Write the positive test**

Create `test/e2e/trait_signature_self_sub_positive.ts`:

```typescript
// RUN: %asc check %s
// Test: Self-substitution accepts impl that returns own<ConcreteType>.
// Registered Clone trait: fn clone(ref<Self>): own<Self>
// After Self -> Foo: fn clone(ref<Self>): own<Foo>
// This is the idiom used throughout std/string.ts, std/json/value.ts, etc.

struct Foo { v: i32 }

impl Clone for Foo {
  fn clone(self: ref<Self>): own<Foo> {
    return Foo { v: self.v };
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run the test**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/e2e/trait_signature_self_sub_positive.ts -v 2>&1 | tail -5`
Expected: PASS (check succeeds).

- [ ] **Step 3: Commit**

```bash
git add test/e2e/trait_signature_self_sub_positive.ts
git commit -m "$(cat <<'EOF'
test(sema): positive test for trait signature Self substitution

Guards the `impl Clone for Foo { fn clone(ref<Self>): own<Foo> }` idiom
used throughout std/. Protects against over-tightening substSelf or
typeEquals in future refactors.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Full suite sanity check

Final verification that all existing tests + 5 new tests pass together.

- [ ] **Step 1: Run full suite**

Run: `PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | tail -5`
Expected: `Passed: 275` (270 baseline + 5 new). Zero failures.

- [ ] **Step 2: Confirm counts by category**

Run:
```bash
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" lit test/ --no-progress-bar 2>&1 | grep -E "Passed|Failed|Unresolved|Unsupported"
```

Expected: single line `Passed: 275 (100.00%)` or equivalent. No failed, unresolved, or unsupported lines.

- [ ] **Step 3: If the count is off, investigate**

- **Fewer than 275**: a new test is regressing OR a signature-fix in Task 4 was wrong. Check `lit test/ -v 2>&1 | grep FAIL`.
- **More than 275**: a previously-failing or XFAIL test flipped. Investigate before declaring success.

No commit for this task — pure verification.

---

## Summary of outputs

When all tasks complete:

- 6 commits on `trait-validator-tightening` branch:
  - Task 1: DiagID additions
  - Task 2: Level 1a (impl unknown trait)
  - Task 3: Level 1b (bound unknown trait)
  - Task 4: Level 2 signature match + broken impl fixes
  - Task 5: Return-type mismatch test
  - Task 6: Self-sub positive test
- 2 new DiagIDs: `ErrUnknownTrait`, `ErrTraitSignatureMismatch`.
- `lib/Sema/SemaDecl.cpp` gains `substSelf`, `typeEquals`, `signaturesMatchAfterSelfSub` helpers and `validateGenericBounds` helper + wiring.
- 5 new lit tests, all passing.
- At minimum 1 existing impl fixed (`operator_add_impl.ts`); possibly more discovered empirically.
- `lit test/` total: **270 → 275 passing**.
- Trait registration count unchanged (**38**).
