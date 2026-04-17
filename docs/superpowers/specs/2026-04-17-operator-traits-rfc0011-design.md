# Operator Traits — RFC-0011 Completion

**Date:** 2026-04-17
**Status:** Design
**Depends on:** RFC-0011 §Operator Traits
**Motivating audit:** Full 20-RFC audit, 2026-04-17 (Builtins.cpp registers 30/38 traits)

## Context

RFC-0011 §Operator Traits specifies that every arithmetic/bitwise/shift operator has a
corresponding trait. The compiler currently registers `Add`, `Sub`, `Mul`, `Div`, `Neg`
but not the remaining binary operator traits or the compound-assignment traits. Users
cannot write `impl Rem for MyType` today — Sema rejects the trait name as undeclared.

The 2026-04-17 RFC audit confirmed this is the single largest gap in RFC-0011 coverage.
All other core traits (Drop, Clone, Copy, Send, Sync, PartialEq, Eq, PartialOrd, Ord,
Hash, Default, Sized, Iterator, IntoIterator, FromIterator, Index, IndexMut, Deref,
DerefMut, Display, Debug, From, Into, AsRef, AsMut) are registered and have correct
signatures per RFC-0011.

## Scope

Register 8 missing traits in `lib/Sema/Builtins.cpp` so that `impl <Trait> for <Type>`
typechecks. This is a registration-only change; it does **not** wire operator dispatch
in `HIRBuilder` — `a % b` will not automatically route to `MyType::rem`. That wiring
is a separate follow-up (the existing `Add`/`Sub`/`Mul`/`Div` registrations are also
not wired to operator dispatch today).

## Design

### Traits to register

Binary operator traits — signature `fn <op>(own<Self>, own<Self>): Self`:

| Trait    | Method   | Surface op |
|----------|----------|------------|
| `Rem`    | `rem`    | `%`        |
| `BitAnd` | `bitand` | `&`        |
| `BitOr`  | `bitor`  | `\|`       |
| `BitXor` | `bitxor` | `^`        |
| `Shl`    | `shl`    | `<<`       |
| `Shr`    | `shr`    | `>>`       |

Compound assignment traits — signature `fn <op>(refmut<Self>, own<Self>): void`:

| Trait       | Method       | Surface op |
|-------------|--------------|------------|
| `AddAssign` | `add_assign` | `+=`       |
| `SubAssign` | `sub_assign` | `-=`       |

**Why only `Add`/`Sub` assign variants:** RFC-0011 explicitly mentions `+=`/`-=` as
driving examples; `MulAssign`/`DivAssign`/etc. are not called out as priorities. YAGNI
until a concrete user need appears.

### Signature choice — following existing pattern

Existing `Add` registration uses `own<Self>, own<Self>) -> Self` without RFC's
`Rhs`/`Output` generalization. This change mirrors that pattern for consistency. If
generalization becomes necessary, it's a cross-cutting refactor affecting all operator
traits and warrants its own RFC clarification.

### File layout

Single file touched: `lib/Sema/Builtins.cpp`. Insert 8 new registration blocks in
two groups:

```
... existing Add, Sub, Mul, Div blocks ...
+ Rem                       (completes arithmetic group)
+ BitAnd, BitOr, BitXor     (bitwise group)
+ Shl, Shr                  (shift group)
... existing Neg block ...
+ AddAssign, SubAssign      (compound-assignment group)
```

Each block follows the established ~30-line pattern: create `Self` type, build
`FunctionDecl` for the method, wrap in `TraitItem`, create `TraitDecl`, register in
`traitDecls` map and declare the `Symbol` in the outer scope.

For `AddAssign`/`SubAssign`, use `RefMutType` for the self parameter (mirroring
`Drop`'s registration), and a `void` return type.

Total estimated change: ~240 LOC added, no existing code modified.

## Test plan

New lit tests in `test/e2e/` (matches existing convention — `trait_default.ts`,
`trait_dispatch.ts`, `trait_impl.ts`, `trait_methods.ts`, `trait_return.ts` all live
there; `test/Sema/` holds only `type_check.ts`):

| File                                     | Trait under test |
|------------------------------------------|------------------|
| `trait_rem_impl.ts`                      | `Rem`            |
| `trait_bitand_impl.ts`                   | `BitAnd`         |
| `trait_bitor_impl.ts`                    | `BitOr`          |
| `trait_bitxor_impl.ts`                   | `BitXor`         |
| `trait_shl_impl.ts`                      | `Shl`            |
| `trait_shr_impl.ts`                      | `Shr`            |
| `trait_add_assign_impl.ts`               | `AddAssign`      |
| `trait_sub_assign_impl.ts`               | `SubAssign`      |
| `trait_rem_signature_mismatch.ts` (neg)  | `Rem` (bad sig)  |

Positive-test skeleton:

```typescript
// RUN: %asc check %s
struct Wrap { v: i32 }

impl Rem for Wrap {
  fn rem(own<Self>, rhs: own<Self>): Self {
    return Wrap { v: self.v % rhs.v };
  }
}

fn main(): i32 { return 0; }
```

Negative test verifies Sema rejects a wrong signature (e.g. `fn rem(ref<Self>, ...)`).

### Success criteria

- All 261 existing lit tests continue to pass.
- 9 new lit tests (8 positive + 1 negative) pass.
- `impl Rem for T`, `impl AddAssign for T`, etc. all typecheck for user-defined types.
- `cmake --build build -j$(nproc) && lit test/ --no-progress-bar` is green.

## Out of scope

- **Operator dispatch wiring.** `a % b` calling `MyType::rem` requires changes in
  `HIRBuilder.cpp` where binary operators are lowered. Follow-up PR.
- **Derive synthesis.** `@derive(Rem)` etc. — not listed as derive-supported in
  RFC-0011; marker-only semantics if used.
- **Remaining `*Assign` traits.** `MulAssign`, `DivAssign`, `RemAssign`,
  `BitAndAssign`, `BitOrAssign`, `BitXorAssign`, `ShlAssign`, `ShrAssign`. Add on
  demand.
- **RFC's full `Rhs`/`Output` generalization.** Existing registrations use the
  simplified `Self-only` form; sticking with that for consistency.

## Risk

Low. Purely additive registration following a well-established pattern in the same
file. Worst-case failure mode: a std library impl uses a different signature than the
registered trait and starts emitting new Sema errors — in that case, either update
the std impl or adjust the signature, same class of fix as S698 (Display/PartialOrd/
Ord/Hash signature corrections).

## Coverage impact

RFC-0011 §Operator Traits lists 10 binary operator traits. Today 4 are registered
(`Add`, `Sub`, `Mul`, `Div`); `Neg` is also registered but is unary. This change
registers the remaining 6 binary traits (`Rem`, `BitAnd`, `BitOr`, `BitXor`, `Shl`,
`Shr`), bringing RFC-0011 binary-operator coverage to **10/10**.

Separately, 2 of the 10 compound-assignment traits (`AddAssign`, `SubAssign`) are
registered, leaving 8 compound-assignment traits as explicit future work.

Total trait registrations: 30 → 38.
