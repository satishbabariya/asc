# Operator Traits — RFC-0011 Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Register 8 missing operator traits (`Rem`, `BitAnd`, `BitOr`, `BitXor`, `Shl`, `Shr`, `AddAssign`, `SubAssign`) in `lib/Sema/Builtins.cpp` so that `impl <Trait> for T` typechecks for user-defined types.

**Architecture:** Additive registration in a single file following the established `Add`/`Sub`/`Mul`/`Div`/`Neg` pattern. Each trait gets a self-contained ~30-line block that constructs a `FunctionDecl` for its method, wraps it in a `TraitItem`/`TraitDecl`, and registers it in `traitDecls` + the outer scope. No HIR, codegen, or operator-dispatch changes.

**Tech Stack:** C++ (LLVM 18 / MLIR 18), CMake, lit + FileCheck for tests. Build via Homebrew `llvm@18`.

---

## File Structure

**Modified files:**
- `lib/Sema/Builtins.cpp` — 8 new registration blocks inserted after the `Div` block (currently ends at line 636), before the `Neg` block (currently starts at line 638). Total +~240 LOC, no existing code changed.

**New test files (all under `test/e2e/`):**
- `trait_rem_impl.ts`
- `trait_bitand_impl.ts`
- `trait_bitor_impl.ts`
- `trait_bitxor_impl.ts`
- `trait_shl_impl.ts`
- `trait_shr_impl.ts`
- `trait_add_assign_impl.ts`
- `trait_sub_assign_impl.ts`
- `trait_rem_signature_mismatch.ts` (negative test)

## Pre-flight: Build baseline

Before starting, verify the tree builds clean and 261 tests pass:

```bash
cd /Users/satishbabariya/Desktop/asc
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/ --no-progress-bar 2>&1 | tail -3
```

Expected output: build succeeds, lit reports `Passed: 261` (or equivalent — commit 714e535 is baseline).

---

## Task 1: Register `Rem` trait (`%` operator)

**Files:**
- Create: `test/e2e/trait_rem_impl.ts`
- Modify: `lib/Sema/Builtins.cpp` (insert after line 636, before `Neg` block)

- [ ] **Step 1: Write the failing test**

Create `test/e2e/trait_rem_impl.ts`:

```typescript
// RUN: %asc check %s
// Test: impl Rem for user-defined struct typechecks.

struct Wrap { v: i32 }

impl Rem for Wrap {
  fn rem(own<Self>, rhs: own<Self>): Self {
    return Wrap { v: self.v % rhs.v };
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/trait_rem_impl.ts -v 2>&1 | tail -10`
Expected: FAIL. Output contains "undeclared identifier 'Rem'" or similar.

- [ ] **Step 3: Register the `Rem` trait in Builtins.cpp**

Open `lib/Sema/Builtins.cpp`. Immediately after line 636 (`  }` closing the `Div` block) and before line 638 (`  // Neg trait:`), insert:

```cpp

  // Rem trait: fn rem(own<Self>, own<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfOwn = ctx.create<OwnType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfOwn;
    selfParam.isSelfRef = false;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<OwnType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *remMethod = ctx.create<FunctionDecl>(
        "rem", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem remItem;
    remItem.method = remMethod;
    auto *remTrait = ctx.create<TraitDecl>(
        "Rem", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{remItem}, loc);
    traitDecls["Rem"] = remTrait;
    Symbol sym;
    sym.name = "Rem";
    sym.decl = remTrait;
    scope->declare("Rem", std::move(sym));
  }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/e2e/trait_rem_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES (`Passed: 1`).

- [ ] **Step 5: Commit**

```bash
git add test/e2e/trait_rem_impl.ts lib/Sema/Builtins.cpp
git commit -m "$(cat <<'EOF'
feat(sema): register Rem trait for % operator

Adds Rem trait registration in Builtins.cpp so `impl Rem for T` typechecks.
Follows existing Add/Sub/Mul/Div pattern. HIR dispatch wiring is separate.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Register `BitAnd` trait (`&` operator)

**Files:**
- Create: `test/e2e/trait_bitand_impl.ts`
- Modify: `lib/Sema/Builtins.cpp` (insert after new `Rem` block)

- [ ] **Step 1: Write the failing test**

Create `test/e2e/trait_bitand_impl.ts`:

```typescript
// RUN: %asc check %s
// Test: impl BitAnd for user-defined struct typechecks.

struct Bits { v: u32 }

impl BitAnd for Bits {
  fn bitand(own<Self>, rhs: own<Self>): Self {
    return Bits { v: self.v & rhs.v };
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/trait_bitand_impl.ts -v 2>&1 | tail -10`
Expected: FAIL with "undeclared identifier 'BitAnd'".

- [ ] **Step 3: Register the `BitAnd` trait in Builtins.cpp**

Immediately after the `Rem` block added in Task 1, insert:

```cpp

  // BitAnd trait: fn bitand(own<Self>, own<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfOwn = ctx.create<OwnType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfOwn;
    selfParam.isSelfRef = false;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<OwnType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *bitandMethod = ctx.create<FunctionDecl>(
        "bitand", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem bitandItem;
    bitandItem.method = bitandMethod;
    auto *bitandTrait = ctx.create<TraitDecl>(
        "BitAnd", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{bitandItem}, loc);
    traitDecls["BitAnd"] = bitandTrait;
    Symbol sym;
    sym.name = "BitAnd";
    sym.decl = bitandTrait;
    scope->declare("BitAnd", std::move(sym));
  }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/e2e/trait_bitand_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES.

- [ ] **Step 5: Commit**

```bash
git add test/e2e/trait_bitand_impl.ts lib/Sema/Builtins.cpp
git commit -m "$(cat <<'EOF'
feat(sema): register BitAnd trait for & operator

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Register `BitOr` trait (`|` operator)

**Files:**
- Create: `test/e2e/trait_bitor_impl.ts`
- Modify: `lib/Sema/Builtins.cpp` (insert after new `BitAnd` block)

- [ ] **Step 1: Write the failing test**

Create `test/e2e/trait_bitor_impl.ts`:

```typescript
// RUN: %asc check %s
// Test: impl BitOr for user-defined struct typechecks.

struct Bits { v: u32 }

impl BitOr for Bits {
  fn bitor(own<Self>, rhs: own<Self>): Self {
    return Bits { v: self.v | rhs.v };
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/trait_bitor_impl.ts -v 2>&1 | tail -10`
Expected: FAIL with "undeclared identifier 'BitOr'".

- [ ] **Step 3: Register the `BitOr` trait in Builtins.cpp**

Immediately after the `BitAnd` block, insert:

```cpp

  // BitOr trait: fn bitor(own<Self>, own<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfOwn = ctx.create<OwnType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfOwn;
    selfParam.isSelfRef = false;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<OwnType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *bitorMethod = ctx.create<FunctionDecl>(
        "bitor", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem bitorItem;
    bitorItem.method = bitorMethod;
    auto *bitorTrait = ctx.create<TraitDecl>(
        "BitOr", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{bitorItem}, loc);
    traitDecls["BitOr"] = bitorTrait;
    Symbol sym;
    sym.name = "BitOr";
    sym.decl = bitorTrait;
    scope->declare("BitOr", std::move(sym));
  }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/e2e/trait_bitor_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES.

- [ ] **Step 5: Commit**

```bash
git add test/e2e/trait_bitor_impl.ts lib/Sema/Builtins.cpp
git commit -m "$(cat <<'EOF'
feat(sema): register BitOr trait for | operator

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Register `BitXor` trait (`^` operator)

**Files:**
- Create: `test/e2e/trait_bitxor_impl.ts`
- Modify: `lib/Sema/Builtins.cpp` (insert after new `BitOr` block)

- [ ] **Step 1: Write the failing test**

Create `test/e2e/trait_bitxor_impl.ts`:

```typescript
// RUN: %asc check %s
// Test: impl BitXor for user-defined struct typechecks.

struct Bits { v: u32 }

impl BitXor for Bits {
  fn bitxor(own<Self>, rhs: own<Self>): Self {
    return Bits { v: self.v ^ rhs.v };
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/trait_bitxor_impl.ts -v 2>&1 | tail -10`
Expected: FAIL with "undeclared identifier 'BitXor'".

- [ ] **Step 3: Register the `BitXor` trait in Builtins.cpp**

Immediately after the `BitOr` block, insert:

```cpp

  // BitXor trait: fn bitxor(own<Self>, own<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfOwn = ctx.create<OwnType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfOwn;
    selfParam.isSelfRef = false;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<OwnType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *bitxorMethod = ctx.create<FunctionDecl>(
        "bitxor", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem bitxorItem;
    bitxorItem.method = bitxorMethod;
    auto *bitxorTrait = ctx.create<TraitDecl>(
        "BitXor", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{bitxorItem}, loc);
    traitDecls["BitXor"] = bitxorTrait;
    Symbol sym;
    sym.name = "BitXor";
    sym.decl = bitxorTrait;
    scope->declare("BitXor", std::move(sym));
  }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/e2e/trait_bitxor_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES.

- [ ] **Step 5: Commit**

```bash
git add test/e2e/trait_bitxor_impl.ts lib/Sema/Builtins.cpp
git commit -m "$(cat <<'EOF'
feat(sema): register BitXor trait for ^ operator

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Register `Shl` trait (`<<` operator)

**Files:**
- Create: `test/e2e/trait_shl_impl.ts`
- Modify: `lib/Sema/Builtins.cpp` (insert after new `BitXor` block)

- [ ] **Step 1: Write the failing test**

Create `test/e2e/trait_shl_impl.ts`:

```typescript
// RUN: %asc check %s
// Test: impl Shl for user-defined struct typechecks.

struct Bits { v: u32 }

impl Shl for Bits {
  fn shl(own<Self>, rhs: own<Self>): Self {
    return Bits { v: self.v << rhs.v };
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/trait_shl_impl.ts -v 2>&1 | tail -10`
Expected: FAIL with "undeclared identifier 'Shl'".

- [ ] **Step 3: Register the `Shl` trait in Builtins.cpp**

Immediately after the `BitXor` block, insert:

```cpp

  // Shl trait: fn shl(own<Self>, own<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfOwn = ctx.create<OwnType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfOwn;
    selfParam.isSelfRef = false;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<OwnType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *shlMethod = ctx.create<FunctionDecl>(
        "shl", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem shlItem;
    shlItem.method = shlMethod;
    auto *shlTrait = ctx.create<TraitDecl>(
        "Shl", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{shlItem}, loc);
    traitDecls["Shl"] = shlTrait;
    Symbol sym;
    sym.name = "Shl";
    sym.decl = shlTrait;
    scope->declare("Shl", std::move(sym));
  }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/e2e/trait_shl_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES.

- [ ] **Step 5: Commit**

```bash
git add test/e2e/trait_shl_impl.ts lib/Sema/Builtins.cpp
git commit -m "$(cat <<'EOF'
feat(sema): register Shl trait for << operator

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Register `Shr` trait (`>>` operator)

**Files:**
- Create: `test/e2e/trait_shr_impl.ts`
- Modify: `lib/Sema/Builtins.cpp` (insert after new `Shl` block)

- [ ] **Step 1: Write the failing test**

Create `test/e2e/trait_shr_impl.ts`:

```typescript
// RUN: %asc check %s
// Test: impl Shr for user-defined struct typechecks.

struct Bits { v: u32 }

impl Shr for Bits {
  fn shr(own<Self>, rhs: own<Self>): Self {
    return Bits { v: self.v >> rhs.v };
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/trait_shr_impl.ts -v 2>&1 | tail -10`
Expected: FAIL with "undeclared identifier 'Shr'".

- [ ] **Step 3: Register the `Shr` trait in Builtins.cpp**

Immediately after the `Shl` block, insert:

```cpp

  // Shr trait: fn shr(own<Self>, own<Self>): Self
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfOwn = ctx.create<OwnType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfOwn;
    selfParam.isSelfRef = false;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<OwnType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *shrMethod = ctx.create<FunctionDecl>(
        "shr", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem shrItem;
    shrItem.method = shrMethod;
    auto *shrTrait = ctx.create<TraitDecl>(
        "Shr", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{shrItem}, loc);
    traitDecls["Shr"] = shrTrait;
    Symbol sym;
    sym.name = "Shr";
    sym.decl = shrTrait;
    scope->declare("Shr", std::move(sym));
  }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/e2e/trait_shr_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES.

- [ ] **Step 5: Commit**

```bash
git add test/e2e/trait_shr_impl.ts lib/Sema/Builtins.cpp
git commit -m "$(cat <<'EOF'
feat(sema): register Shr trait for >> operator

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Register `AddAssign` trait (`+=` operator)

`AddAssign`'s signature differs — `refmut<Self>` self, `own<Self>` rhs, returns `void`. Pattern mirrors `Drop` (line 242), not `Add`.

**Files:**
- Create: `test/e2e/trait_add_assign_impl.ts`
- Modify: `lib/Sema/Builtins.cpp` (insert after new `Shr` block)

- [ ] **Step 1: Write the failing test**

Create `test/e2e/trait_add_assign_impl.ts`:

```typescript
// RUN: %asc check %s
// Test: impl AddAssign for user-defined struct typechecks.

struct Counter { v: i32 }

impl AddAssign for Counter {
  fn add_assign(refmut<Self>, rhs: own<Self>): void {
    self.v = self.v + rhs.v;
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/trait_add_assign_impl.ts -v 2>&1 | tail -10`
Expected: FAIL with "undeclared identifier 'AddAssign'".

- [ ] **Step 3: Register the `AddAssign` trait in Builtins.cpp**

Immediately after the `Shr` block, insert:

```cpp

  // AddAssign trait: fn add_assign(refmut<Self>, own<Self>): void
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRefMut = ctx.create<RefMutType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRefMut;
    selfParam.isSelfRefMut = true;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<OwnType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *addAssignMethod = ctx.create<FunctionDecl>(
        "add_assign", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.getVoidType(), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem addAssignItem;
    addAssignItem.method = addAssignMethod;
    auto *addAssignTrait = ctx.create<TraitDecl>(
        "AddAssign", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{addAssignItem}, loc);
    traitDecls["AddAssign"] = addAssignTrait;
    Symbol sym;
    sym.name = "AddAssign";
    sym.decl = addAssignTrait;
    scope->declare("AddAssign", std::move(sym));
  }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/e2e/trait_add_assign_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES.

- [ ] **Step 5: Commit**

```bash
git add test/e2e/trait_add_assign_impl.ts lib/Sema/Builtins.cpp
git commit -m "$(cat <<'EOF'
feat(sema): register AddAssign trait for += operator

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Register `SubAssign` trait (`-=` operator)

**Files:**
- Create: `test/e2e/trait_sub_assign_impl.ts`
- Modify: `lib/Sema/Builtins.cpp` (insert after new `AddAssign` block)

- [ ] **Step 1: Write the failing test**

Create `test/e2e/trait_sub_assign_impl.ts`:

```typescript
// RUN: %asc check %s
// Test: impl SubAssign for user-defined struct typechecks.

struct Counter { v: i32 }

impl SubAssign for Counter {
  fn sub_assign(refmut<Self>, rhs: own<Self>): void {
    self.v = self.v - rhs.v;
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/trait_sub_assign_impl.ts -v 2>&1 | tail -10`
Expected: FAIL with "undeclared identifier 'SubAssign'".

- [ ] **Step 3: Register the `SubAssign` trait in Builtins.cpp**

Immediately after the `AddAssign` block, insert:

```cpp

  // SubAssign trait: fn sub_assign(refmut<Self>, own<Self>): void
  {
    auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
    auto *selfRefMut = ctx.create<RefMutType>(selfType, loc);
    ParamDecl selfParam;
    selfParam.name = "self";
    selfParam.type = selfRefMut;
    selfParam.isSelfRefMut = true;
    selfParam.loc = loc;
    ParamDecl rhsParam;
    rhsParam.name = "rhs";
    rhsParam.type = ctx.create<OwnType>(
        ctx.create<NamedType>("Self", std::vector<Type *>{}, loc), loc);
    rhsParam.loc = loc;
    auto *subAssignMethod = ctx.create<FunctionDecl>(
        "sub_assign", std::vector<GenericParam>{},
        std::vector<ParamDecl>{selfParam, rhsParam},
        ctx.getVoidType(), nullptr,
        std::vector<WhereConstraint>{}, loc);
    TraitItem subAssignItem;
    subAssignItem.method = subAssignMethod;
    auto *subAssignTrait = ctx.create<TraitDecl>(
        "SubAssign", std::vector<GenericParam>{},
        std::vector<Type *>{},
        std::vector<TraitItem>{subAssignItem}, loc);
    traitDecls["SubAssign"] = subAssignTrait;
    Symbol sym;
    sym.name = "SubAssign";
    sym.decl = subAssignTrait;
    scope->declare("SubAssign", std::move(sym));
  }
```

- [ ] **Step 4: Rebuild and run test**

Run:
```bash
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -3
lit test/e2e/trait_sub_assign_impl.ts -v 2>&1 | tail -5
```

Expected: build succeeds, test PASSES.

- [ ] **Step 5: Commit**

```bash
git add test/e2e/trait_sub_assign_impl.ts lib/Sema/Builtins.cpp
git commit -m "$(cat <<'EOF'
feat(sema): register SubAssign trait for -= operator

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Negative test — Rem signature mismatch

Verify that providing a wrong-signature impl for `Rem` produces a Sema diagnostic. Guards against regressions that silently loosen the registered signature.

**Files:**
- Create: `test/e2e/trait_rem_signature_mismatch.ts`

- [ ] **Step 1: Write the negative test**

Create `test/e2e/trait_rem_signature_mismatch.ts`:

```typescript
// RUN: not %asc check %s
// Test: impl Rem with wrong signature (ref<Self> instead of own<Self>) is rejected.

struct Wrap { v: i32 }

impl Rem for Wrap {
  fn rem(ref<Self>, rhs: ref<Self>): Self {
    return Wrap { v: self.v % rhs.v };
  }
}

function main(): i32 { return 0; }
```

The `RUN: not` prefix inverts the exit code: lit expects `asc check` to fail.

- [ ] **Step 2: Run the test**

Run: `lit test/e2e/trait_rem_signature_mismatch.ts -v 2>&1 | tail -10`
Expected: PASS (because `asc check` failed as expected). If the test fails, it means Sema accepted the wrong signature — that is itself a bug to investigate before committing.

- [ ] **Step 3: Commit**

```bash
git add test/e2e/trait_rem_signature_mismatch.ts
git commit -m "$(cat <<'EOF'
test(sema): guard Rem trait signature against regressions

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Full suite sanity check

Final verification that all 261 existing tests + 9 new tests pass together, and that the full `Passed` count increased by exactly 9.

- [ ] **Step 1: Run the full lit suite**

Run:
```bash
lit test/ --no-progress-bar 2>&1 | tail -10
```

Expected: `Passed: 270` (= baseline 261 + 9 new). Zero failures, zero unexpected passes.

- [ ] **Step 2: If the count is off, investigate**

- Fewer than 270 passing: one of the new tests is regressing — look at `Failed` / `Unexpectedly Passed` output.
- More than 270 passing: some previously-failing or XFAIL test flipped to passing — investigate before claiming success; likely benign but confirm.

- [ ] **Step 3: Final verification with `--verbose` summary**

Run:
```bash
lit test/ --no-progress-bar 2>&1 | grep -E "Passed|Failed|Timed Out|Unresolved"
```

Expected: `Passed: 270` only. No other lines.

No commit for this task — it's pure verification.

---

## Summary of outputs

When all tasks complete:

- 9 new commits on the current branch (8 feature + 1 test-guard).
- `lib/Sema/Builtins.cpp` gains 8 trait registration blocks (~240 LOC).
- `test/e2e/` gains 9 new test files.
- RFC-0011 binary-operator trait coverage: **4/10 → 10/10**.
- Compound-assignment trait coverage: **0/10 → 2/10** (`AddAssign`, `SubAssign`).
- Total trait registrations: **30 → 38**.
- `lit test/` passing count: **261 → 270**.
