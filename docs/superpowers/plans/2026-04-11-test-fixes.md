# Fix 6 Test-Discovered Issues — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 5 bugs found during comprehensive testing (issues 3+6 merged into one).

**Architecture:** Surgical fixes across CodeGen, Sema, and Driver. Each task is independently committable.

**Tech Stack:** C++20, LLVM/MLIR 18, lit tests

---

## File Map

| File | Action | Fix |
|------|--------|-----|
| `lib/CodeGen/CodeGen.cpp:179` | Modify | O0 codegen for all targets |
| `lib/Sema/SemaType.cpp` | Modify | Monomorphized type compatibility |
| `lib/Sema/SemaDecl.cpp:229-237` | Modify | EnumPattern binding in let-else |
| `lib/Sema/SemaExpr.cpp:1205` | Modify | Option reassignment compatibility |
| `lib/Driver/Driver.cpp:606-648` | Modify | Wasm auto-link with runtime |
| `test/e2e/native_target.ts` | Create | Native O2 test |
| `test/e2e/while_let_reassign.ts` | Create | while let reassignment test |
| `test/e2e/let_else_scope.ts` | Create | let-else pattern scope test |
| `test/e2e/generic_return.ts` | Create | Generic struct return test |

---

### Task 1: Fix Legacy PM O2 Crash on All Targets

**Files:**
- Modify: `lib/CodeGen/CodeGen.cpp:179`
- Create: `test/e2e/native_target.ts`

- [ ] **Step 1: Fix codegen opt level**

In `lib/CodeGen/CodeGen.cpp`, line 179, change:
```cpp
  auto codegenOpt = triple.isWasm() ? llvm::CodeGenOptLevel::None
                                     : getLLVMOptLevel();
```
to:
```cpp
  // Use O0 for the legacy PM codegen on all targets. Optimization is handled
  // by the new-PM in runLLVMOptPasses(). The legacy PM crashes at O2+ due to
  // pass scheduling issues in LLVM 18 (affects both Wasm and native backends).
  auto codegenOpt = llvm::CodeGenOptLevel::None;
```

- [ ] **Step 2: Create native target test**

Create `test/e2e/native_target.ts`:
```typescript
// RUN: %asc build %s --target aarch64-apple-darwin --emit obj -o %t.o
// Tests that native target compilation works at default O2.

function fib(n: i32): i32 {
  if (n <= 1) { return n; }
  return fib(n - 1) + fib(n - 2);
}

function main(): i32 {
  return fib(10);
}
```

- [ ] **Step 3: Build, test, commit**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep "error:"
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/native_target.ts -v
lit test/ --no-progress-bar 2>&1 | tail -5
git add lib/CodeGen/CodeGen.cpp test/e2e/native_target.ts
git commit -m "fix: use O0 codegen level for all targets — prevents legacy PM crash at O2"
```

---

### Task 2: Fix Generic Struct Return Type Compatibility

**Files:**
- Modify: `lib/Sema/SemaType.cpp`
- Create: `test/e2e/generic_return.ts`

- [ ] **Step 1: Create test**

Create `test/e2e/generic_return.ts`:
```typescript
// RUN: %asc check %s

struct Pair<T> { first: T, second: T }

function make_pair(a: i32, b: i32): Pair<i32> {
  return Pair { first: a, second: b };
}

function main(): i32 {
  let p = make_pair(3, 4);
  return p.first;
}
```

- [ ] **Step 2: Fix isCompatible for monomorphized names**

In `lib/Sema/SemaType.cpp`, in the `isCompatible` function, find the named-type comparison (around line 91-95):
```cpp
  if (auto *ln = dynamic_cast<NamedType *>(lhs)) {
    if (auto *rn = dynamic_cast<NamedType *>(rhs))
      return ln->getName() == rn->getName();
    return false;
  }
```

Replace with:
```cpp
  if (auto *ln = dynamic_cast<NamedType *>(lhs)) {
    if (auto *rn = dynamic_cast<NamedType *>(rhs)) {
      if (ln->getName() == rn->getName())
        return true;
      // Check if one is a monomorphization of the other.
      // e.g., "Pair_i32" is compatible with "Pair" if "Pair_i32" exists in monoCache.
      auto lname = ln->getName();
      auto rname = rn->getName();
      if (monoCache.count(lname) && rname.starts_with(lname.substr(0, lname.find('_'))))
        return true;
      if (monoCache.count(rname) && lname.starts_with(rname.substr(0, rname.find('_'))))
        return true;
      return false;
    }
    return false;
  }
```

- [ ] **Step 3: Build, test, commit**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep "error:"
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/generic_return.ts -v
lit test/ --no-progress-bar 2>&1 | tail -5
git add lib/Sema/SemaType.cpp test/e2e/generic_return.ts
git commit -m "fix: monomorphized type names compatible with generic base types"
```

---

### Task 3: Fix let-else Pattern Variable Scope

**Files:**
- Modify: `lib/Sema/SemaDecl.cpp:229-237`
- Create: `test/e2e/let_else_scope.ts`

- [ ] **Step 1: Create test**

Create `test/e2e/let_else_scope.ts`:
```typescript
// RUN: %asc check %s

function get_value(opt: Option<i32>): i32 {
  let Option::Some(v) = opt else {
    return 0;
  };
  return v;
}

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Add EnumPattern handling in checkVarDecl**

In `lib/Sema/SemaDecl.cpp`, find the pattern binding section (around line 229-237). After the `TuplePattern` handler, add `EnumPattern` handling:

Replace lines 229-237:
```cpp
    if (auto *sp = dynamic_cast<SlicePattern *>(d->getPattern())) {
      for (auto *elem : sp->getElements())
        declarePatternBindings(elem);
    } else if (auto *tp = dynamic_cast<TuplePattern *>(d->getPattern())) {
      for (auto *elem : tp->getElements())
        declarePatternBindings(elem);
    } else {
      declarePatternBindings(d->getPattern());
    }
```

With:
```cpp
    if (auto *sp = dynamic_cast<SlicePattern *>(d->getPattern())) {
      for (auto *elem : sp->getElements())
        declarePatternBindings(elem);
    } else if (auto *tp = dynamic_cast<TuplePattern *>(d->getPattern())) {
      for (auto *elem : tp->getElements())
        declarePatternBindings(elem);
    } else if (auto *ep = dynamic_cast<EnumPattern *>(d->getPattern())) {
      // Bind enum pattern args: let Option::Some(v) = expr
      // Look up the enum variant to get payload types.
      const auto &path = ep->getPath();
      std::vector<Type *> payloadTypes;
      if (path.size() >= 2 && type) {
        if (auto *nt = dynamic_cast<NamedType *>(type)) {
          auto eit = enumDecls.find(nt->getName());
          if (eit != enumDecls.end()) {
            for (auto *v : eit->second->getVariants()) {
              if (v->getName() == path.back() && !v->getTupleTypes().empty()) {
                payloadTypes = std::vector<Type *>(
                    v->getTupleTypes().begin(), v->getTupleTypes().end());
                break;
              }
            }
          }
        }
      }
      for (unsigned i = 0; i < ep->getArgs().size(); ++i) {
        if (auto *ip = dynamic_cast<IdentPattern *>(ep->getArgs()[i])) {
          Symbol sym;
          sym.name = ip->getName().str();
          sym.decl = d;
          sym.type = (i < payloadTypes.size()) ? payloadTypes[i] : type;
          sym.isMutable = !d->isConst();
          sym.ownership = ownerInfo;
          currentScope->declare(ip->getName(), std::move(sym));
        }
      }
    } else {
      declarePatternBindings(d->getPattern());
    }
```

- [ ] **Step 3: Build, test, commit**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep "error:"
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/let_else_scope.ts -v
lit test/ --no-progress-bar 2>&1 | tail -5
git add lib/Sema/SemaDecl.cpp test/e2e/let_else_scope.ts
git commit -m "fix: let-else binds enum pattern variables into enclosing scope"
```

---

### Task 4: Fix while let Option Reassignment

**Files:**
- Modify: `lib/Sema/SemaExpr.cpp:1205`
- Create: `test/e2e/while_let_reassign.ts`

- [ ] **Step 1: Create test**

Create `test/e2e/while_let_reassign.ts`:
```typescript
// RUN: %asc check %s

function main(): i32 {
  let x: Option<i32> = Option::Some(42);
  let result: i32 = 0;
  while let Option::Some(v) = x {
    result = v;
    x = Option::None;
  }
  return result;
}
```

- [ ] **Step 2: Fix assignment compatibility for enum variants**

In `lib/Sema/SemaExpr.cpp`, in `checkAssignExpr` (around line 1205), before the type mismatch error, add a check for enum variant assignment:

Replace:
```cpp
  if (targetType && valueType && !isCompatible(targetType, valueType)) {
    diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                    "assignment type mismatch");
  }
```

With:
```cpp
  if (targetType && valueType && !isCompatible(targetType, valueType)) {
    // Allow assigning enum variants to their parent enum type.
    // e.g., x: Option<i32> = Option::None (None is base "Option" type)
    bool isEnumVariantAssign = false;
    if (auto *tn = dynamic_cast<NamedType *>(targetType)) {
      if (auto *vn = dynamic_cast<NamedType *>(valueType)) {
        // Check if value type is the base enum and target is monomorphized.
        // e.g., target = "Option_i32", value = "Option"
        llvm::StringRef tname = tn->getName();
        llvm::StringRef vname = vn->getName();
        if (tname.starts_with(vname) && tname.size() > vname.size() &&
            tname[vname.size()] == '_') {
          isEnumVariantAssign = true;
        }
        // Also check reverse: target = "Option", value = "Option_i32"
        if (vname.starts_with(tname) && vname.size() > tname.size() &&
            vname[tname.size()] == '_') {
          isEnumVariantAssign = true;
        }
      }
    }
    if (!isEnumVariantAssign) {
      diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                      "assignment type mismatch");
    }
  }
```

- [ ] **Step 3: Build, test, commit**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep "error:"
cd /Users/satishbabariya/Desktop/asc && lit test/e2e/while_let_reassign.ts -v
lit test/ --no-progress-bar 2>&1 | tail -5
git add lib/Sema/SemaExpr.cpp test/e2e/while_let_reassign.ts
git commit -m "fix: allow assigning enum variants to monomorphized enum variables"
```

---

### Task 5: Fix Wasm Auto-Link with Runtime

**Files:**
- Modify: `lib/Driver/Driver.cpp:606-648`

- [ ] **Step 1: Fix linkWasm to include runtime and use --export=_start**

In `lib/Driver/Driver.cpp`, replace the `linkWasm` method (lines 606-648):

```cpp
ExitCode Driver::linkWasm(const std::string &objFile,
                          const std::string &outFile) {
  // Find wasm-ld in PATH or in LLVM install directory.
  auto wasmLdPath = llvm::sys::findProgramByName("wasm-ld");
  if (!wasmLdPath) {
    llvm::errs() << "error: wasm-ld not found in PATH; "
                 << "cannot link Wasm output\n";
    return ExitCode::SystemError;
  }

  // Compile runtime to a temp object file.
  // Find runtime.c relative to the compiler binary or source tree.
  std::string runtimeObj;
  auto clangPath = llvm::sys::findProgramByName("clang");
  if (clangPath) {
    runtimeObj = outFile + ".rt.o";
    // Look for runtime.c in common locations.
    std::vector<std::string> runtimePaths = {
      "lib/Runtime/runtime.c",                    // build dir
      "../lib/Runtime/runtime.c",                  // installed
    };
    std::string runtimeSrc;
    for (const auto &p : runtimePaths) {
      if (llvm::sys::fs::exists(p)) { runtimeSrc = p; break; }
    }
    if (!runtimeSrc.empty()) {
      llvm::SmallVector<llvm::StringRef, 8> clangArgs;
      clangArgs.push_back(*clangPath);
      clangArgs.push_back("--target=wasm32-wasi");
      clangArgs.push_back("-c");
      clangArgs.push_back(runtimeSrc);
      clangArgs.push_back("-o");
      clangArgs.push_back(runtimeObj);
      std::string errMsg;
      int rc = llvm::sys::ExecuteAndWait(*clangPath, clangArgs,
                                          std::nullopt, {}, 60, 0, &errMsg);
      if (rc != 0) runtimeObj.clear(); // Failed to compile runtime.
    }
  }

  // Build wasm-ld argument list.
  llvm::SmallVector<llvm::StringRef, 12> args;
  args.push_back(*wasmLdPath);
  args.push_back(objFile);
  if (!runtimeObj.empty())
    args.push_back(runtimeObj);
  args.push_back("-o");
  args.push_back(outFile);
  args.push_back("--export=_start");
  args.push_back("--allow-undefined");

  if (opts.verbose) {
    llvm::errs() << "  [link] ";
    for (auto &a : args) llvm::errs() << a << " ";
    llvm::errs() << "\n";
  }

  std::string errMsg;
  int rc = llvm::sys::ExecuteAndWait(*wasmLdPath, args,
                                      std::nullopt, {}, 60, 0, &errMsg);

  // Clean up runtime object.
  if (!runtimeObj.empty())
    std::remove(runtimeObj.c_str());

  if (rc != 0) {
    llvm::errs() << "error: wasm-ld failed";
    if (!errMsg.empty()) llvm::errs() << ": " << errMsg;
    llvm::errs() << "\n";
    return ExitCode::SystemError;
  }

  return ExitCode::Success;
}
```

- [ ] **Step 2: Build, test, commit**

```bash
cd /Users/satishbabariya/Desktop/asc/build && cmake --build . -j$(sysctl -n hw.ncpu) 2>&1 | grep "error:"
cd /Users/satishbabariya/Desktop/asc && lit test/ --no-progress-bar 2>&1 | tail -5
# Manual Wasm test:
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" ./build/tools/asc/asc build test/e2e/hello_i32.ts --target wasm32-wasi -o /tmp/link_test.wasm
wasmtime /tmp/link_test.wasm; echo "Exit: $?"
git add lib/Driver/Driver.cpp
git commit -m "fix: Wasm auto-link includes runtime and uses --export=_start"
```

---

### Task 6: Final Verification

- [ ] **Step 1: Run all 5 original failing tests**

```bash
cd /Users/satishbabariya/Desktop/asc
echo "=== while let ==="
./build/tools/asc/asc check test/e2e/while_let_reassign.ts 2>&1
echo "=== let-else ==="
./build/tools/asc/asc check test/e2e/let_else_scope.ts 2>&1
echo "=== native O2 ==="
./build/tools/asc/asc build test/e2e/native_target.ts --target aarch64-apple-darwin --emit obj -o /tmp/native.o 2>&1
echo "=== generic return ==="
./build/tools/asc/asc check test/e2e/generic_return.ts 2>&1
echo "=== wasm auto-link ==="
PATH="/opt/homebrew/opt/llvm@18/bin:$PATH" ./build/tools/asc/asc build /tmp/test_wasm.ts --target wasm32-wasi -o /tmp/final.wasm 2>&1 && wasmtime /tmp/final.wasm; echo "fib(10) = $?"
```

- [ ] **Step 2: Full test suite**

```bash
lit test/ --no-progress-bar 2>&1 | tail -5
```

Expected: 172+ tests pass (168 existing + 4 new), 0 failures.
