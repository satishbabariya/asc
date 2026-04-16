# Derive Macro Expansion — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `@derive(Clone)` synthesize a real `fn clone()` method and `@derive(PartialEq)` synthesize a real `fn eq()` method for structs, so derived types can actually use these traits.

**Architecture:** Add a pre-pass in `Sema::analyze()` that scans structs for derive marker attributes (`@clone`, `@partialeq`) and synthesizes AST `ImplDecl` nodes. These synthetic impls are appended to the items list and processed by the existing two-pass analysis + HIRBuilder pipeline. No changes to HIR or codegen.

**Tech Stack:** C++ (AST node creation via ASTContext), cmake, lit tests.

---

### Task 1: Synthesize derive(Clone) impl blocks

**Files:**
- Modify: `lib/Sema/Sema.cpp` (add synthesis function + call in `analyze()`)

- [ ] **Step 1: Update derive_clone test to actually call clone()**

Update `test/e2e/derive_clone.ts`:

```typescript
// RUN: %asc check %s
// Test: @derive(Clone) generates callable clone method.

@derive(Clone)
struct Point { x: i32, y: i32 }

function main(): i32 {
  let p = Point { x: 10, y: 20 };
  let q = p.clone();
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/derive_clone.ts -v`
Expected: FAIL — `clone` method not found on `Point` (no impl generated)

- [ ] **Step 3: Add synthesizeDeriveImpls function**

In `lib/Sema/Sema.cpp`, add a new function before `Sema::analyze()`:

```cpp
/// Synthesize impl blocks for @derive attributes on structs.
/// Appends synthetic ImplDecl nodes to `items`.
static void synthesizeDeriveImpls(ASTContext &ctx,
                                   std::vector<Decl *> &items) {
  // Collect synthetic impls in a separate vector to avoid invalidating
  // the items iteration.
  std::vector<Decl *> syntheticImpls;

  for (auto *item : items) {
    auto *sd = dynamic_cast<StructDecl *>(item);
    if (!sd) continue;

    SourceLocation loc = sd->getLocation();
    std::string typeName = sd->getName().str();

    // Check for @clone attribute (set by @derive(Clone) expansion in SemaDecl).
    bool hasClone = false;
    for (const auto &attr : sd->getAttributes()) {
      if (attr == "@clone") { hasClone = true; break; }
    }

    if (hasClone) {
      // Synthesize: impl Clone for Type { fn clone(ref<Self>): own<Type> { ... } }
      //
      // Body: return Type { field1: self.field1, field2: self.field2, ... };
      // For primitive fields, field access copies the value (primitives are @copy).
      // For owned fields, this should call .clone() but for simplicity we
      // use direct field access (works for @copy fields, which is what
      // derive(Clone) requires — all fields must be Clone).

      // Build struct literal with field inits from self.
      std::vector<FieldInit> fieldInits;
      for (auto *field : sd->getFields()) {
        auto *selfRef = ctx.create<DeclRefExpr>("self", loc);
        auto *fieldAccess = ctx.create<FieldAccessExpr>(
            selfRef, field->getName().str(), loc);
        FieldInit fi;
        fi.name = field->getName().str();
        fi.value = fieldAccess;
        fi.loc = loc;
        fieldInits.push_back(fi);
      }

      auto *structLit = ctx.create<StructLiteral>(
          typeName, std::move(fieldInits), /*spreadExpr=*/nullptr, loc);
      auto *retStmt = ctx.create<ReturnStmt>(structLit, loc);
      auto *body = ctx.create<CompoundStmt>(
          std::vector<Stmt *>{retStmt}, /*trailingExpr=*/nullptr, loc);

      // Create self parameter: ref<Self>
      auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
      auto *selfRefType = ctx.create<RefType>(selfType, loc);
      ParamDecl selfParam;
      selfParam.name = "self";
      selfParam.type = selfRefType;
      selfParam.isSelfRef = true;
      selfParam.loc = loc;

      // Return type: own<Type>
      auto *retType = ctx.create<NamedType>(typeName, std::vector<Type *>{}, loc);

      auto *cloneMethod = ctx.create<FunctionDecl>(
          "clone", std::vector<GenericParam>{},
          std::vector<ParamDecl>{selfParam},
          retType, body, std::vector<WhereConstraint>{}, loc);

      // Create impl Clone for Type
      auto *targetType = ctx.create<NamedType>(typeName, std::vector<Type *>{}, loc);
      auto *traitType = ctx.create<NamedType>("Clone", std::vector<Type *>{}, loc);
      auto *implDecl = ctx.create<ImplDecl>(
          std::vector<GenericParam>{}, targetType, traitType,
          std::vector<FunctionDecl *>{cloneMethod}, loc);

      syntheticImpls.push_back(implDecl);
    }
  }

  // Append all synthetic impls to items.
  for (auto *impl : syntheticImpls)
    items.push_back(impl);
}
```

- [ ] **Step 4: Call synthesizeDeriveImpls at the start of analyze()**

In `lib/Sema/Sema.cpp`, in `Sema::analyze()` (line 47), add a call before the first pass:

```cpp
void Sema::analyze(std::vector<Decl *> &items) {
  // Synthesize impl blocks for @derive attributes before analysis.
  synthesizeDeriveImpls(ctx, items);

  // First pass: register all type declarations and impl blocks.
  for (auto *item : items) {
```

Insert the `synthesizeDeriveImpls(ctx, items);` line right after the opening `{` of `analyze()`, before the existing comment.

- [ ] **Step 5: Rebuild and test**

Run: `cmake --build build/ -j$(sysctl -n hw.ncpu) && lit test/e2e/derive_clone.ts -v`

Expected: Build succeeds. Test passes (clone method is now synthesized and resolvable).

- [ ] **Step 6: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All 245 tests pass.

- [ ] **Step 7: Commit**

```bash
git add lib/Sema/Sema.cpp test/e2e/derive_clone.ts
git commit -m "feat: @derive(Clone) synthesizes real clone() impl for structs (RFC-0015)"
```

### Task 2: Add derive(PartialEq) synthesis

**Files:**
- Modify: `lib/Sema/Sema.cpp` (extend synthesizeDeriveImpls)

- [ ] **Step 1: Update derive_partialeq test to call eq()**

Update `test/e2e/derive_partialeq.ts`:

```typescript
// RUN: %asc check %s
// Test: @derive(PartialEq) generates callable eq method.

@derive(PartialEq)
struct Color { r: i32, g: i32, b: i32 }

function main(): i32 {
  let a = Color { r: 1, g: 2, b: 3 };
  let b = Color { r: 1, g: 2, b: 3 };
  assert!(a.eq(&b));
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `lit test/e2e/derive_partialeq.ts -v`

Expected: FAIL — `eq` method not found on `Color`

- [ ] **Step 3: Add PartialEq synthesis to synthesizeDeriveImpls**

In `lib/Sema/Sema.cpp`, inside `synthesizeDeriveImpls`, after the `if (hasClone) { ... }` block and before the closing `}` of the for loop, add:

```cpp
    // Check for @partialeq attribute.
    bool hasPartialEq = false;
    for (const auto &attr : sd->getAttributes()) {
      if (attr == "@partialeq") { hasPartialEq = true; break; }
    }

    if (hasPartialEq) {
      // Synthesize: impl PartialEq for Type {
      //   fn eq(ref<Self>, other: ref<Type>): bool {
      //     return self.f1 == other.f1 && self.f2 == other.f2 && ...;
      //   }
      // }

      // Build chained && comparison of all fields.
      Expr *comparison = nullptr;

      for (auto *field : sd->getFields()) {
        std::string fname = field->getName().str();
        auto *selfRef = ctx.create<DeclRefExpr>("self", loc);
        auto *selfField = ctx.create<FieldAccessExpr>(selfRef, fname, loc);
        auto *otherRef = ctx.create<DeclRefExpr>("other", loc);
        auto *otherField = ctx.create<FieldAccessExpr>(otherRef, fname, loc);

        auto *fieldEq = ctx.create<BinaryExpr>(
            BinaryOp::Eq, selfField, otherField, loc);

        if (comparison == nullptr) {
          comparison = fieldEq;
        } else {
          comparison = ctx.create<BinaryExpr>(
              BinaryOp::LogAnd, comparison, fieldEq, loc);
        }
      }

      // For structs with no fields, return true.
      if (comparison == nullptr) {
        comparison = ctx.create<BoolLiteral>(true, loc);
      }

      auto *retStmt = ctx.create<ReturnStmt>(comparison, loc);
      auto *body = ctx.create<CompoundStmt>(
          std::vector<Stmt *>{retStmt}, /*trailingExpr=*/nullptr, loc);

      // Self parameter: ref<Self>
      auto *selfType = ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
      auto *selfRefType = ctx.create<RefType>(selfType, loc);
      ParamDecl selfParam;
      selfParam.name = "self";
      selfParam.type = selfRefType;
      selfParam.isSelfRef = true;
      selfParam.loc = loc;

      // Other parameter: ref<Type>
      auto *otherNamedType = ctx.create<NamedType>(
          typeName, std::vector<Type *>{}, loc);
      auto *otherRefType = ctx.create<RefType>(otherNamedType, loc);
      ParamDecl otherParam;
      otherParam.name = "other";
      otherParam.type = otherRefType;
      otherParam.loc = loc;

      // Return type: bool
      auto *boolType = ctx.getBuiltinType(BuiltinTypeKind::Bool);

      auto *eqMethod = ctx.create<FunctionDecl>(
          "eq", std::vector<GenericParam>{},
          std::vector<ParamDecl>{selfParam, otherParam},
          boolType, body, std::vector<WhereConstraint>{}, loc);

      // Create impl PartialEq for Type
      auto *targetType = ctx.create<NamedType>(
          typeName, std::vector<Type *>{}, loc);
      auto *traitType = ctx.create<NamedType>(
          "PartialEq", std::vector<Type *>{}, loc);
      auto *implDecl = ctx.create<ImplDecl>(
          std::vector<GenericParam>{}, targetType, traitType,
          std::vector<FunctionDecl *>{eqMethod}, loc);

      syntheticImpls.push_back(implDecl);
    }
```

**Note:** The `BoolLiteral` class may not exist. If the compiler doesn't have `BoolLiteral`, use `ctx.create<IntegerLiteral>(1, ctx.getBuiltinType(BuiltinTypeKind::Bool), loc)` or whatever boolean literal representation exists. Check `include/asc/AST/Expr.h` for the exact class name.

- [ ] **Step 4: Rebuild and test**

Run: `cmake --build build/ -j$(sysctl -n hw.ncpu) && lit test/e2e/derive_partialeq.ts -v`

Expected: Build succeeds. Test passes.

- [ ] **Step 5: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All 245 tests pass.

- [ ] **Step 6: Commit**

```bash
git add lib/Sema/Sema.cpp test/e2e/derive_partialeq.ts
git commit -m "feat: @derive(PartialEq) synthesizes real eq() impl for structs (RFC-0015)"
```

### Task 3: Final Validation

- [ ] **Step 1: Run full test suite**

Run: `lit test/ --no-progress-bar`

Expected: All tests pass.

- [ ] **Step 2: Verify both derive tests exercise the methods**

Run: `lit test/e2e/derive_clone.ts test/e2e/derive_partialeq.ts -v`

Expected: Both PASS.

- [ ] **Step 3: Check git status**

Run: `git status && git log --oneline -3`

Expected: Clean tree, two new commits for derive expansion.
