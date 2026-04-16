# Derive Macro Expansion — Clone and PartialEq

| Field | Value |
|---|---|
| Date | 2026-04-16 |
| Goal | Make @derive(Clone) and @derive(PartialEq) generate real impl blocks with callable methods |
| Baseline | 245/245 tests, derive attributes parse but produce no code |
| Target | @derive(Clone) produces fn clone(), @derive(PartialEq) produces fn eq() |

## Motivation

`@derive(Clone, PartialEq, ...)` currently parses in Sema and adds marker attributes (`@clone`, `@partialeq`), but no impl blocks or methods are ever synthesized. The tests just verify the attribute parses — they can't actually call `.clone()` or `.eq()` on derived types. This is the key blocker for idiomatic struct usage.

## Current State

**`lib/Sema/SemaDecl.cpp:49-115`:** The derive expansion loop reads `@derive(T1, T2)`, extracts trait names, and calls `d->addAttribute("@clone")` etc. For `derive(Copy)`, it validates all fields are Copy and adds `@copy`. For all other traits, it just adds a marker attribute that nothing consumes.

**`lib/HIR/HIRBuilder.cpp:533-574`:** `visitImplDecl` processes impl blocks — emits methods with mangled names (`Type_method`), registers `__drop_TypeName` for Drop impls, generates vtables. This is the target for synthesized impl blocks.

## Design

### Approach: AST Synthesis in Sema

After the existing derive attribute expansion (line 115 of SemaDecl.cpp), add a new function `synthesizeDeriveImpls` that:

1. Checks if a struct has `@clone` or `@partialeq` attributes
2. Creates synthetic `ImplDecl` AST nodes with the appropriate method bodies
3. Appends them to the top-level items list so they flow through `visitImplDecl` normally

This is the cleanest approach because:
- Synthetic impls get full type checking via the normal Sema pipeline
- HIRBuilder's `visitImplDecl` handles method mangling, vtable generation, drop registration
- No special codegen path needed — everything uses the existing infrastructure

### derive(Clone) Implementation

For a struct:
```typescript
@derive(Clone)
struct Point { x: i32, y: i32 }
```

Synthesize:
```typescript
impl Clone for Point {
  fn clone(ref<Self>): own<Point> {
    return Point { x: self.x.clone(), y: self.y.clone() };
  }
}
```

**Sema synthesis logic:**
1. For each field of the struct, create a `FieldAccessExpr` on `self`, then wrap in a `CallExpr` to `.clone()`
2. For primitive/builtin fields (i32, bool, etc.), the clone is a value copy — the Sema type checker handles this since primitives are `@copy`
3. Create a `StructLiteralExpr` with all cloned fields
4. Wrap in a `ReturnStmt` inside a `FunctionDecl` named "clone"
5. Create an `ImplDecl` targeting `Point` implementing `Clone` with this method

### derive(PartialEq) Implementation

For a struct:
```typescript
@derive(PartialEq)
struct Color { r: i32, g: i32, b: i32 }
```

Synthesize:
```typescript
impl PartialEq for Color {
  fn eq(ref<Self>, other: ref<Color>): bool {
    return self.r == other.r && self.g == other.g && self.b == other.b;
  }
}
```

**Sema synthesis logic:**
1. For each field, create a `BinaryExpr(==)` comparing `self.field` and `other.field`
2. Chain all comparisons with `BinaryExpr(&&)` (left-associative fold)
3. For a struct with 0 fields, return `true`
4. Wrap in a `ReturnStmt` inside a `FunctionDecl` named "eq"
5. Create an `ImplDecl` targeting the struct implementing `PartialEq`

### Where to Add Code

**`lib/Sema/SemaDecl.cpp`:** Add `synthesizeDeriveImpls(StructDecl *d, std::vector<Decl*> &items)` called after the attribute expansion loop. This function checks for `@clone`/`@partialeq` attributes and creates synthetic ImplDecl/FunctionDecl/Expr nodes via the ASTContext allocator.

**Key AST creation APIs (already exist in ASTContext):**
- `ctx.create<ImplDecl>(targetType, traitType, methods, loc)`
- `ctx.create<FunctionDecl>(name, generics, params, returnType, body, constraints, loc)`
- `ctx.create<ReturnStmt>(expr, loc)`
- `ctx.create<StructLiteralExpr>(typeName, fields, loc)`
- `ctx.create<FieldAccessExpr>(base, fieldName, loc)`
- `ctx.create<BinaryExpr>(op, lhs, rhs, loc)`
- `ctx.create<CallExpr>(callee, args, loc)`
- `ctx.create<DeclRefExpr>(name, loc)`

All nodes are bump-allocated by ASTContext — no ownership concerns.

## Testing

Update existing tests to actually exercise the derived methods:

**`test/e2e/derive_clone.ts`:**
```typescript
// RUN: %asc check %s
@derive(Clone)
struct Point { x: i32, y: i32 }

function main(): i32 {
  let p = Point { x: 1, y: 2 };
  let q = p.clone();
  return 0;
}
```

**`test/e2e/derive_partialeq.ts`:**
```typescript
// RUN: %asc check %s
@derive(PartialEq)
struct Color { r: i32, g: i32, b: i32 }

function main(): i32 {
  let a = Color { r: 1, g: 2, b: 3 };
  let b = Color { r: 1, g: 2, b: 3 };
  assert!(a.eq(&b));
  return 0;
}
```

## What This Does NOT Include

- derive(Debug) — needs Formatter type resolution and string concatenation
- derive(Hash) — needs Hasher type resolution
- derive(Default) — needs default value construction for each type
- derive(Serialize/Deserialize) — needs full attribute system from RFC-0016
- Enum derives — only struct derives in this phase
