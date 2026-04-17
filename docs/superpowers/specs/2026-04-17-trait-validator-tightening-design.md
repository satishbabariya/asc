# Trait Validator Tightening

**Date:** 2026-04-17
**Status:** Design
**Depends on:** RFC-0011; follow-up to `2026-04-17-operator-traits-rfc0011-design.md`
**Motivating discovery:** During the 2026-04-17 operator-traits work, we found that `checkImplDecl` in `lib/Sema/SemaDecl.cpp` silently accepts `impl <UnknownTrait> for T` and does not validate that impl method signatures match the registered trait. This makes trait registration effectively advisory.

## Context

`lib/Sema/SemaDecl.cpp:179-233` (`Sema::checkImplDecl`) is the sole validator for `impl Trait for T` blocks. Its current logic:

1. Register the impl for method resolution.
2. If the impl is a trait impl AND the trait name resolves in `traitDecls`, verify every non-default trait method is present by name on the impl.
3. If the trait name does NOT resolve → **silently accept**.
4. Method parameter types and return types are **never compared** against the trait declaration.

Two concrete bugs follow:

- `impl BogusTrait for T` compiles cleanly. Same for `function f<T: BogusTrait>(x: T)`.
- `impl Add for Counter { fn add(self: ref<Counter>, other: ref<Counter>): Counter }` compiles cleanly against a registered `Add` trait with `fn add(own<Self>, own<Self>): Self`. The impl uses shared borrows where the trait specifies ownership transfer — a real semantic mismatch.

This RFC-follow-up tightens both checks.

## Scope

### Level 1 — Unknown trait rejection

Reject references to unknown traits in two locations:

- `impl <Trait> for T` — at `SemaDecl.cpp:189`, add an `else` branch for the trait-not-found case.
- `function/impl method<T: <Trait>>` generic bounds. Grep confirms there is **no existing bound-trait validator** — `GenericParam.bounds` is stored on the AST and consulted during monomorphization (`SemaType.cpp:323-368`) but no code currently verifies that the named trait exists in `traitDecls`. The check goes in `Sema::checkFunctionDecl` (`SemaDecl.cpp:5`) or `Sema::checkStructDecl`/`checkTraitDecl` — wherever generics are introduced. A single iteration over `d->getGenericParams()` checking each bound name against `traitDecls` covers function, struct, enum, trait, and impl declaration sites.

### Level 2 — Signature match after Self substitution

For each `impl Trait for ConcreteType` block where `Trait` resolves in `traitDecls`, walk the trait's required methods (those with no default body). For each, find the corresponding impl method by name and compare signatures after substituting `Self → ConcreteType`:

- Parameter counts must match.
- For each parameter, the trait's declared type — with `Self` replaced by `ConcreteType` — must equal the impl's declared type.
- Return type, same rule.

**Equality is structural, exact:** no variance, no subtyping, no widening/narrowing. If the trait says `own<Self>` and the impl says `ref<Self>`, that's an error.

### Fix broken existing impls

Running the tightened validator against the current repo will reveal impls with genuine mismatches. Known: `test/e2e/operator_add_impl.ts`. Likely: `test/e2e/operator_traits.ts` and possibly some std numeric-wrapper impls. Each is fixed by updating the signature to match the registered trait — typically a one-line change per impl method.

## Design

### Files modified

- **`lib/Sema/SemaDecl.cpp`** — extend `checkImplDecl` with unknown-trait `else` branch and signature comparison loop. Add file-local helper `signaturesMatchAfterSelfSub(TraitMethod, ImplMethod, ConcreteSelf)`.
- **`lib/Sema/SemaDecl.cpp`** (cont.) — add a new helper `validateGenericBounds(const std::vector<GenericParam> &params, SourceLocation loc)` called from `checkFunctionDecl`, `checkStructDecl`, `checkEnumDecl`, `checkTraitDecl`, and `checkImplDecl`. Iterates each param's bounds and emits `ErrUnknownTrait` for any bound name not in `traitDecls`.
- **`include/asc/Basic/Diagnostic.h`** (or equivalent registry) — add two new DiagIDs: `ErrUnknownTrait`, `ErrTraitSignatureMismatch`.
- **`test/e2e/operator_add_impl.ts`** and other impls surfaced by running the suite — fix signatures.
- **New test files** — see §Testing.

### `signaturesMatchAfterSelfSub` algorithm

```
bool match(TraitMethod *t, ImplMethod *i, Type *concreteSelf):
  if t->paramCount != i->paramCount: return false
  for each (tp, ip) in zip(t->params, i->params):
    if !typeEquals(substSelf(tp->type, concreteSelf), ip->type):
      return false
  if !typeEquals(substSelf(t->returnType, concreteSelf), i->returnType):
    return false
  return true
```

`substSelf(T, C)` walks `T` replacing every `NamedType("Self")` with `C`, preserving wrapping types (`OwnType`, `RefType`, `RefMutType`, etc.). A helper likely needs to be added if one doesn't already exist.

`typeEquals(A, B)` uses pointer identity first (bump-allocated types are canonicalized where possible), with a fallback to a structural compare by kind + inner-type recursion for cases where canonicalization doesn't apply.

### Diagnostic format

```
error[E###]: unknown trait 'BogusTrait'
  --> src/file.ts:L:C
   |
   | impl BogusTrait for T {
   |      ^^^^^^^^^^^ not a registered trait
```

```
error[E###]: method 'add' signature does not match trait 'Add'
  --> src/file.ts:L:C
   |
   |   fn add(self: ref<Counter>, rhs: ref<Counter>): Counter {
   |   ^^^^^^
note: expected signature per trait 'Add':
   |   fn add(self: own<Counter>, rhs: own<Counter>): Counter
```

Error codes follow the existing E### convention (highest currently-assigned code, from `lib/Analysis/`, is E007; new codes will continue the sequence in `include/asc/Basic/Diagnostic.h`).

## Testing

### New lit tests in `test/e2e/`

| File | What it tests | RUN directive |
|---|---|---|
| `trait_unknown_name_impl.ts` | `impl BogusTrait for T` | `// RUN: not %asc check %s` |
| `trait_unknown_name_bound.ts` | `function f<T: BogusTrait>(x: T)` | `// RUN: not %asc check %s` |
| `trait_signature_mismatch_param.ts` | `impl Rem for T` with `ref<Self>` in place of `own<Self>` | `// RUN: not %asc check %s` |
| `trait_signature_mismatch_return.ts` | `impl Rem for T` with wrong return type | `// RUN: not %asc check %s` |
| `trait_signature_correct_self_sub.ts` | `impl Clone for Foo` with `fn clone(ref<Self>): own<Foo>` | `// RUN: %asc check %s` (positive; verifies Self-substitution accepts the common idiom) |

### Fix existing impls

Run `lit test/` after each validator change. Each failing impl is fixed by updating its signature to match the registered trait. Expected targets:

- `test/e2e/operator_add_impl.ts` — confirmed mismatch (`ref<Counter>` → `own<Self>`, return `Counter` → `own<Counter>`).
- `test/e2e/operator_traits.ts` — similar pattern.
- Possibly some std library `Add`/`Sub`/`Mul`/`Div` impls — discovered by running the suite after Level 2 lands; fixed in the same PR.

### Success criteria

- All 5 new lit tests pass (4 negative + 1 positive).
- Existing passing tests remain passing after signature fixes.
- Final `lit test/` count: **275 passing** (270 baseline + 5 new).
- `grep -c "traitDecls\[\"" lib/Sema/Builtins.cpp` stays at 38 (no new registrations; this PR only tightens).

## Out of scope

- Generic type parameters on traits (e.g. `Index<Idx>`). Current registrations don't model these, so nothing to validate.
- Associated types (`Iterator::Item`, `Deref::Target`). Same reason.
- Where clauses on trait methods. Same reason.
- Variance-aware subtyping (allowing `ref<T>` where trait says `own<T>`). Explicitly rejected: the tightening goal is enforcing exact contracts.
- Trait inheritance (`Ord: Eq + PartialOrd`) — the registered traits don't model supertrait bounds either.

If any existing breaking impl can't be fixed with a simple signature update because it depends on one of these excluded features, scope will be narrowed during implementation and the remaining gap documented.

## Risk

Medium. The tightening is correct, but we're increasing strictness against a body of existing code that relied on the loose validator. Mitigation:

- Fix broken impls within the same PR, not as a follow-up.
- Exact-match rule is unambiguous — no judgment calls on what counts as "close enough".
- If the blast radius is larger than a few impls (say, >10 failures requiring nontrivial rewrites), pause and reassess before forcing the strict rule.

## Coverage impact

RFC-0011 "Core Traits" spec requires impls to conform to registered trait signatures. Before this change, conformance is advisory. After, conformance is enforced by Sema. This materially increases the value of every prior and future trait registration, including the 8 operator traits registered in PR #40.
