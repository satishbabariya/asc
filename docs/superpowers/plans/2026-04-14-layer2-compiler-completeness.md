# Layer 2: Compiler Completeness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring RFCs 0001–0004, 0010, 0015 to 100% — derive system, constant folding, debug info, syntax completions, toolchain features.

**Architecture:** 14 tasks targeting Parser, Sema, HIRBuilder, Driver, and CodeGen. The critical path is Task 1 (derive system) which Layer 3 depends on. Tasks 2–14 are largely independent.

**Tech Stack:** C++17, MLIR (LLVM 18), lit test framework

**Baseline:** 211 tests, 209 passing (2 pre-existing failures). Target: ~235 tests.

---

### Task 1: Derive Macro System (RFC-0002, RFC-0011) — CRITICAL

**Files:**
- Modify: `lib/Sema/SemaDecl.cpp`
- Modify: `include/asc/Sema/Sema.h`
- Create: `test/e2e/derive_clone.ts`
- Create: `test/e2e/derive_copy_error.ts`

- [ ] **Step 1: Create test for derive(Clone)**

Create `test/e2e/derive_clone.ts`:
```typescript
// RUN: %asc check %s
// Test: @derive(Clone) generates field-by-field clone.

@derive(Clone)
struct Point { x: i32, y: i32 }

function main(): i32 {
  let p = Point { x: 1, y: 2 };
  let p2 = p.clone();
  return 0;
}
```

Create `test/e2e/derive_copy_error.ts`:
```typescript
// RUN: %asc check %s 2>&1 | grep -q "Copy"
// Test: @derive(Copy) on struct with non-Copy field produces error.

struct Heap { data: own<i32> }

@derive(Copy)
struct Bad { h: Heap }

function main(): i32 { return 0; }
```

- [ ] **Step 2: Run tests to verify they fail**

- [ ] **Step 3: Implement expandDerive in SemaDecl.cpp**

In `lib/Sema/SemaDecl.cpp`, add `expandDerive()` that's called during struct/enum declaration checking. When a declaration has `@derive(TraitName)`:

1. Parse the derive attribute to extract trait names
2. For each trait, generate a synthetic impl block:
   - **Clone**: Generate `clone(ref<Self>): Self` that clones each field
   - **Copy**: Verify all fields are Copy (emit error if not), mark type as @copy
   - **PartialEq**: Generate `eq(ref<Self>, ref<Self>): bool` comparing each field
   - **Eq**: Marker — verify PartialEq exists
   - **Hash**: Generate `hash(ref<Self>, refmut<Hasher>)` hashing each field
   - **Debug**: Generate `fmt(ref<Self>, refmut<Formatter>): void` formatting fields
   - **Default**: Generate `default(): Self` with default value per field
   - **Send/Sync**: Verify all fields satisfy Send/Sync

3. Inject synthetic impls into the type's method table so they're visible to Sema and HIRBuilder

- [ ] **Step 4: Build and test**
- [ ] **Step 5: Run full suite, commit**

---

### Task 2: Constant Folding (RFC-0003)

**Files:**
- Modify: `lib/Driver/Driver.cpp`
- Create: `test/e2e/const_fold.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc build %s --emit mlir > %t.out 2>&1
// RUN: grep -c "arith.addi" %t.out | grep -q "0"
// Test: constant expressions are folded at compile time.

function main(): i32 {
  let x: i32 = 2 + 3;
  return x;
}
```

- [ ] **Step 2: Add canonicalizer pass to pipeline**

In `lib/Driver/Driver.cpp`, in `runTransforms()`, add the MLIR canonicalizer pass before the existing transform passes. The canonicalizer folds constant arithmetic automatically:

```cpp
#include "mlir/Transforms/Passes.h"
// In runTransforms, before EscapeAnalysis:
pm.addPass(mlir::createCanonicalizerPass());
```

- [ ] **Step 3: Build, test, full suite, commit**

---

### Task 3: Template Literal Desugaring (RFC-0002)

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp` (~line 4914, visitTemplateLiteralExpr)
- Create: `test/e2e/template_literal.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "__asc_string" %t.out
// Test: template literal produces string concatenation.

function main(): i32 {
  let name: i32 = 42;
  let msg = `value is ${name}`;
  return 0;
}
```

- [ ] **Step 2: Implement visitTemplateLiteralExpr**

Replace the stub at ~line 4914 with code that:
1. For each string segment, emit a string constant
2. For each expression hole, visit the expression and convert to string (call __asc_i32_to_string or similar runtime helper)
3. Concatenate all segments using __asc_string_concat runtime calls

- [ ] **Step 3: Build, test, full suite, commit**

---

### Task 4: for-in Iterator Desugaring (RFC-0015)

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp` (for loop handling)
- Create: `test/e2e/for_in_desugar.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc build %s --emit llvmir --target aarch64-apple-darwin > %t.out 2>&1
// RUN: grep -q "call" %t.out
// Test: for-in loop over range works.

function main(): i32 {
  let sum: i32 = 0;
  for i in 0..10 {
    sum = sum + i;
  }
  return sum;
}
```

- [ ] **Step 2: Verify for-in already works for ranges**

The parser already handles `for var in expr`. Check if `for i in 0..10` already desugars properly. If so, this task may be partially done.

- [ ] **Step 3: If for-in over collections (Vec, etc.) doesn't work, add IntoIterator desugaring**

In the for loop handler in HIRBuilder:
1. Check if iterable implements IntoIterator
2. Call `.into_iter()` on the collection
3. Emit loop: call `.next()`, match against Some/None, bind variable

- [ ] **Step 4: Build, test, full suite, commit**

---

### Task 5: Operator Trait Dispatch (RFC-0015, RFC-0011)

**Files:**
- Modify: `lib/HIR/HIRBuilder.cpp` (binary expression handling)
- Create: `test/e2e/operator_dispatch.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc check %s
// Test: custom operator traits dispatch correctly.

struct Vec2 { x: i32, y: i32 }

impl Add for Vec2 {
  fn add(self: ref<Vec2>, other: ref<Vec2>): Vec2 {
    return Vec2 { x: self.x + other.x, y: self.y + other.y };
  }
}

function main(): i32 {
  let a = Vec2 { x: 1, y: 2 };
  let b = Vec2 { x: 3, y: 4 };
  let c = a + b;
  return 0;
}
```

- [ ] **Step 2: Check if operator dispatch already exists**

Read HIRBuilder's binary expression handler. The test/e2e/operator_traits.ts already exists and passes — operator dispatch may already work.

- [ ] **Step 3: If not working, add trait lookup in binary op emission**

When emitting binary ops, check if operand type has a matching operator trait impl. If so, emit func.call to the trait method instead of arith op.

- [ ] **Step 4: Build, test, full suite, commit**

---

### Task 6: Labeled Break with Value (RFC-0015)

**Files:**
- Modify: `lib/Parse/ParseStmt.cpp` or `ParseExpr.cpp`
- Modify: `lib/HIR/HIRBuilder.cpp`
- Create: `test/e2e/labeled_break_value.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc check %s
// Test: labeled loop break with value.

function main(): i32 {
  let result = 'outer: loop {
    break 'outer 42;
  };
  return result;
}
```

- [ ] **Step 2: Check existing labeled loop support**

Read existing labelled_loop.ts test to see what's implemented. If break with value already works, just verify.

- [ ] **Step 3: If needed, extend parser to allow expression after break 'label**
- [ ] **Step 4: Build, test, full suite, commit**

---

### Task 7: Nested Pattern Matching (RFC-0015)

**Files:**
- Modify: `lib/Sema/SemaExpr.cpp` (match exhaustiveness)
- Create: `test/e2e/nested_match_exhaustive.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc check %s 2>&1 | grep -q "W003\|exhaustive"
// Test: nested pattern exhaustiveness check.

enum Inner { A, B }
enum Outer { X(Inner), Y }

function check(o: Outer): i32 {
  match o {
    Outer::X(Inner::A) => 1,
    Outer::Y => 3,
  }
}

function main(): i32 { return 0; }
```

- [ ] **Step 2: Enhance exhaustiveness checking in SemaExpr.cpp**

Extend the match exhaustiveness checker to recurse into nested patterns. Currently it checks flat variants. For nested enums, decompose into pattern tree and check coverage at each level.

- [ ] **Step 3: Build, test, full suite, commit**

---

### Task 8: Wasm Feature Gating (RFC-0004)

**Files:**
- Modify: `lib/Driver/Driver.cpp`
- Create: `test/e2e/wasm_features.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc build %s --target wasm32-wasi --emit llvmir > %t.out 2>&1
// RUN: grep -q "wasm32" %t.out
// Test: wasm target with default features compiles.

function main(): i32 {
  return 42;
}
```

- [ ] **Step 2: Add --wasm-features flag**

In Driver.cpp argument parsing, add `--wasm-features=+feature,-feature` flag. Parse comma-separated features, build subtarget features string, pass to LLVM TargetMachine.

- [ ] **Step 3: Build, test, full suite, commit**

---

### Task 9: GPU Target Stubs (RFC-0004)

**Files:**
- Modify: `lib/Driver/Driver.cpp`
- Create: `test/e2e/gpu_target.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc build %s --target nvptx64-nvidia-cuda --emit llvmir > %t.out 2>&1
// RUN: grep -q "target triple" %t.out
// Test: GPU target accepted with experimental warning.

function add(a: i32, b: i32): i32 {
  return a + b;
}
function main(): i32 { return 0; }
```

- [ ] **Step 2: Accept GPU triples in Driver**

Add nvptx64-nvidia-cuda and amdgcn-amd-amdhsa to the target handling. Create TargetMachine, emit warning "GPU target support is experimental".

- [ ] **Step 3: Build, test, full suite, commit**

---

### Task 10: DWARF Debug Info (RFC-0010)

**Files:**
- Modify: `lib/Driver/Driver.cpp`
- Create: `test/e2e/debug_info.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc build %s --debug --target aarch64-apple-darwin --emit llvmir > %t.out 2>&1
// RUN: grep -q "DISubprogram\|!dbg" %t.out
// Test: --debug flag emits DWARF debug metadata.

function add(a: i32, b: i32): i32 {
  return a + b;
}
function main(): i32 { return add(1, 2); }
```

- [ ] **Step 2: Add debug metadata emission**

When `--debug` is set, configure LLVM TargetMachine with debug info. MLIR locations (FileLineCol) already exist from parsing. During LLVM IR translation, these become DILocation metadata if the module has debug info enabled.

Add to the LLVM IR translation step:
```cpp
if (opts.debugInfo) {
  // Enable debug info generation in LLVM module
  module->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                        llvm::DEBUG_METADATA_VERSION);
}
```

- [ ] **Step 3: Build, test, full suite, commit**

---

### Task 11: LSP Go-to-Definition and References (RFC-0010)

**Files:**
- Modify: `lib/Driver/Driver.cpp` (LSP section)
- Create: `test/e2e/lsp_definition.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc lsp --test-mode 2>&1 | grep -q "definition\|capabilities"
// Test: LSP advertises go-to-definition capability.

function main(): i32 { return 0; }
```

- [ ] **Step 2: Add textDocument/definition handler**

In the LSP server loop, handle `textDocument/definition` requests:
1. Parse the position from the request
2. Run Sema on the file to build symbol table
3. Find symbol at position
4. Return the definition location

- [ ] **Step 3: Add textDocument/references handler**

Similar to definition but returns all use sites of the symbol.

- [ ] **Step 4: Build, test, full suite, commit**

---

### Task 12: Fix-it Hints (RFC-0010)

**Files:**
- Modify: `lib/Sema/SemaExpr.cpp`
- Modify: `lib/Sema/SemaDecl.cpp`
- Create: `test/e2e/fixit_hints.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc check %s 2>&1 | grep -q "hint\|suggest\|try"
// Test: diagnostic includes fix-it suggestion.

function main(): i32 {
  let _unused: i32 = 42;
  return 0;
}
```

- [ ] **Step 2: Add fix-it text to common diagnostics**

Attach suggestion text to common errors:
- Missing semicolon → "insert ';' here"
- Unused variable without _ prefix → "prefix with '_' to suppress warning"
- Type mismatch with obvious cast → "try 'as TargetType'"

- [ ] **Step 3: Build, test, full suite, commit**

---

### Task 13: Enhance asc fmt (RFC-0010)

**Files:**
- Modify: `lib/Driver/Driver.cpp` (runFmt section)
- Create: `test/e2e/fmt_roundtrip.ts`

- [ ] **Step 1: Create test**

```typescript
// RUN: %asc fmt %s > %t.out 2>&1
// RUN: diff %s %t.out
// Test: well-formatted file is unchanged by formatter.

function add(a: i32, b: i32): i32 {
  return a + b;
}

function main(): i32 {
  let x: i32 = add(1, 2);
  return x;
}
```

- [ ] **Step 2: Enhance formatting rules**

The formatter already exists (lines 241-338 in Driver.cpp). Enhance:
- Consistent brace placement (K&R style)
- Operator spacing
- Line length limit (100 chars)
- Verify roundtrip: format → reparse produces same AST

- [ ] **Step 3: Build, test, full suite, commit**

---

### Task 14: Self-Hosting Validation Gate

**Files:**
- Create: `test/e2e/self_hosting_canary.ts`

- [ ] **Step 1: Create canary test**

```typescript
// RUN: %asc check %s
// Test: self-hosting canary — exercises generics, traits, closures, patterns, for-in.

struct Wrapper<T> { value: T }

impl Wrapper<i32> {
  fn get(self: ref<Wrapper<i32>>): i32 {
    return self.value;
  }
}

function apply(f: (i32) => i32, x: i32): i32 {
  return f(x);
}

function main(): i32 {
  let w = Wrapper<i32> { value: 42 };
  let v = w.get();
  let doubled = apply((x: i32): i32 => { return x * 2; }, v);

  let result: i32 = 0;
  match doubled {
    84 => { result = 1; },
    _ => { result = 0; },
  }

  return result;
}
```

- [ ] **Step 2: Run and verify**

This test exercises generics, trait methods, closures, and pattern matching — the features Layer 3 std library code will need. If it passes, the compiler is ready for self-hosted libraries.

- [ ] **Step 3: Build, test, full suite, commit**

---

## Task Dependency Graph

```
Task 1 (derive system) ──── CRITICAL PATH
Task 2 (const folding) ──┐
Task 3 (template lit)  ──┤
Task 4 (for-in desugar) ─┤── all independent
Task 5 (operator dispatch)┤
Task 6 (labeled break)  ─┤
Task 7 (nested match)   ─┤
Task 8 (wasm features)  ─┤
Task 9 (GPU targets)    ─┤
Task 10 (DWARF debug)   ─┤
Task 11 (LSP enhance)   ─┤
Task 12 (fix-it hints)  ─┤
Task 13 (fmt enhance)   ─┘
Task 14 (canary test)  ──── depends on Tasks 1, 4, 5 (validates all features)
```
