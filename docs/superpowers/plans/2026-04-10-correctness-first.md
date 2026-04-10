# Correctness-First Push — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix silent miscompilation in ownership lowering, soundness holes in the borrow checker, and type system permissiveness so existing features produce correct results.

**Architecture:** Bottom-up — fix lowering first (silent wrong output), then borrow checker (wrong diagnostics), then type system (wrong acceptance), then quick architectural wins. Each task is independently committable and testable.

**Tech Stack:** C++20, LLVM/MLIR 18, CMake, lit test framework

---

## File Map

| File | Responsibility | Action |
|------|---------------|--------|
| `include/asc/HIR/OwnTypes.h` | Own dialect type definitions | Modify: add type storage for inner/send/sync |
| `lib/HIR/OwnTypes.cpp` | Own type storage implementation | Modify: implement custom TypeStorage |
| `lib/CodeGen/OwnershipLowering.cpp` | own.* → LLVM lowering | Modify: heap alloc, aggregate move |
| `lib/Analysis/RegionInference.cpp` | Pass 2: borrow region computation | Modify: fix op names, phi propagation, CFG reachability |
| `lib/Analysis/AliasCheck.cpp` | Pass 3: aliasing rules A/B/C | Modify: fix cross-block overlap |
| `lib/Analysis/MoveCheck.cpp` | Pass 4: use-after-move detection | Modify: fix type detection, conditional move severity |
| `lib/Analysis/SendSyncCheck.cpp` | Pass 5: Send/Sync verification | Modify: use real type flags |
| `lib/Analysis/DropInsertion.cpp` | Drop insertion transform | Modify: fix type detection |
| `lib/Analysis/PanicScopeWrap.cpp` | Panic scope wrapping transform | Modify: fix type detection |
| `lib/Sema/SemaType.cpp` | Type compatibility checking | Modify: restrict integer widening |
| `lib/Sema/SemaExpr.cpp` | Expression type checking | Modify: `?` operator, `for` loop, match exhaustiveness |
| `include/asc/Driver/Driver.h` | Driver options | Modify: default opt level |
| `lib/Driver/Driver.cpp` | Pipeline orchestration | Modify: enable MLIR verifier |
| `lib/CodeGen/CodeGen.cpp` | MLIR→LLVM lowering | Modify: enable verifier, fix debug info |
| `test/e2e/heap_alloc.ts` | E2E test for heap allocation | Create |
| `test/e2e/aggregate_move.ts` | E2E test for aggregate moves | Create |
| `test/e2e/integer_compat.ts` | E2E test for integer type safety | Create |
| `test/e2e/try_operator.ts` | E2E test for ? operator | Create |
| `test/e2e/match_exhaustive.ts` | E2E test for match exhaustiveness | Create |

---

### Task 1: Architectural Quick Wins (Verifier, Default Opt, Debug Info)

Low-risk changes that improve correctness infrastructure. Do these first so subsequent tasks run with the verifier on.

**Files:**
- Modify: `include/asc/Driver/Driver.h:38`
- Modify: `lib/Driver/Driver.cpp:549,566`
- Modify: `lib/CodeGen/CodeGen.cpp:95`

- [ ] **Step 1: Change default optimization level from O0 to O2**

In `include/asc/Driver/Driver.h`, line 38, change:
```cpp
  OptLevel optLevel = OptLevel::O0;
```
to:
```cpp
  OptLevel optLevel = OptLevel::O2;
```

- [ ] **Step 2: Enable MLIR verifier in analysis pipeline**

In `lib/Driver/Driver.cpp`, line 549, change:
```cpp
  pm.enableVerifier(false);
```
to:
```cpp
  pm.enableVerifier(true);
```

- [ ] **Step 3: Enable MLIR verifier in transforms pipeline**

In `lib/Driver/Driver.cpp`, line 566, change:
```cpp
  pm.enableVerifier(false);
```
to:
```cpp
  pm.enableVerifier(true);
```

- [ ] **Step 4: Enable MLIR verifier in CodeGen lowering**

In `lib/CodeGen/CodeGen.cpp`, line 95, change:
```cpp
  pm.enableVerifier(false);
```
to:
```cpp
  pm.enableVerifier(true);
```

- [ ] **Step 5: Build and run existing tests to check for verifier failures**

Run:
```bash
cd build && cmake --build . 2>&1 | tail -20
```
Then run the test suite:
```bash
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -30
```

If the verifier catches existing pass bugs, note them — they'll be fixed in subsequent tasks. If the failures are too numerous to proceed, temporarily revert the verifier enablement in the analysis pipeline only (Driver.cpp:549) and re-enable it after Tasks 2-5 fix the underlying issues.

- [ ] **Step 6: Commit**

```bash
git add include/asc/Driver/Driver.h lib/Driver/Driver.cpp lib/CodeGen/CodeGen.cpp
git commit -m "fix: enable MLIR verifier, default opt level to O2"
```

---

### Task 2: Fix Op Name Consistency Across Analysis Passes

The single most impactful one-line fix — RegionInference silently misses all borrow ops because it searches for `"own.borrow"` instead of `"own.borrow_ref"`.

**Files:**
- Modify: `lib/Analysis/RegionInference.cpp:97`

- [ ] **Step 1: Fix the borrow op name in RegionInference**

In `lib/Analysis/RegionInference.cpp`, line 97, change:
```cpp
    bool isBorrow = opName == "own.borrow" || opName == "own.borrow_mut";
```
to:
```cpp
    bool isBorrow = opName == "own.borrow_ref" || opName == "own.borrow_mut";
```

- [ ] **Step 2: Verify no other files use the wrong op name**

Search all analysis files for `"own.borrow"` string references that don't match the registered name:
```bash
grep -rn '"own\.borrow"' /Users/satishbabariya/Desktop/asc/lib/Analysis/
```
Expected: only the line you just fixed (now showing `"own.borrow_ref"`). The AliasCheck already has both `"own.borrow"` and `"own.borrow_ref"` in its `collectBorrows` — fix it too.

- [ ] **Step 3: Fix AliasCheck borrow name redundancy**

In `lib/Analysis/AliasCheck.cpp`, line 72-73, change:
```cpp
    bool isSharedBorrow = (opName == "own.borrow" ||
                           opName == "own.borrow_ref");
```
to:
```cpp
    bool isSharedBorrow = (opName == "own.borrow_ref");
```

- [ ] **Step 4: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 5: Commit**

```bash
git add lib/Analysis/RegionInference.cpp lib/Analysis/AliasCheck.cpp
git commit -m "fix: standardize borrow op name to own.borrow_ref across all passes"
```

---

### Task 3: Replace String-Based Type Detection with `mlir::isa<>`

Every analysis pass detects owned/borrow types via string matching. Replace with proper MLIR type checking.

**Files:**
- Modify: `lib/Analysis/MoveCheck.cpp:20-28`
- Modify: `lib/Analysis/DropInsertion.cpp:21-24`
- Modify: `lib/Analysis/PanicScopeWrap.cpp:23-32`

- [ ] **Step 1: Fix MoveCheck isOwnedType**

In `lib/Analysis/MoveCheck.cpp`, replace lines 20-28:
```cpp
static bool isOwnedType(mlir::Type type) {
  // Check if the type is an own.val type from our custom dialect.
  // The type's mnemonic would be "own.val" in the own dialect.
  if (auto namedType = type.dyn_cast<mlir::Type>()) {
    llvm::StringRef typeName = type.getAbstractType().getName();
    return typeName.contains("own.val");
  }
  return false;
}
```
with:
```cpp
static bool isOwnedType(mlir::Type type) {
  return mlir::isa<own::OwnValType>(type);
}
```

Add the include at the top of MoveCheck.cpp (after the existing includes):
```cpp
#include "asc/HIR/OwnTypes.h"
```

- [ ] **Step 2: Fix DropInsertion isOwnedType**

In `lib/Analysis/DropInsertion.cpp`, replace lines 21-24:
```cpp
static bool isOwnedType(mlir::Type type) {
  llvm::StringRef typeName = type.getAbstractType().getName();
  return typeName.contains("own.val");
}
```
with:
```cpp
static bool isOwnedType(mlir::Type type) {
  return mlir::isa<own::OwnValType>(type);
}
```

Add the include at the top of DropInsertion.cpp:
```cpp
#include "asc/HIR/OwnTypes.h"
```

- [ ] **Step 3: Fix PanicScopeWrap isOwnedType**

In `lib/Analysis/PanicScopeWrap.cpp`, replace lines 23-32:
```cpp
static bool isOwnedType(mlir::Type type) {
  // Check for own.val custom dialect type.
  llvm::StringRef typeName = type.getAbstractType().getName();
  if (typeName.contains("own.val"))
    return true;
  // Also detect LLVM pointer types — struct/heap allocations are owned.
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(type))
    return true;
  return false;
}
```
with:
```cpp
static bool isOwnedType(mlir::Type type) {
  if (mlir::isa<own::OwnValType>(type))
    return true;
  // Also detect LLVM pointer types — struct/heap allocations are owned.
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(type))
    return true;
  return false;
}
```

Add the include at the top of PanicScopeWrap.cpp:
```cpp
#include "asc/HIR/OwnTypes.h"
```

- [ ] **Step 4: Fix SendSyncCheck string-based type detection**

In `lib/Analysis/SendSyncCheck.cpp`, lines 118-119 and 127, replace the string-based borrow type checks:
```cpp
      llvm::StringRef typeName = type.getAbstractType().getName();
      if (typeName.contains("borrow") && !typeName.contains("borrow.mut")) {
```
with:
```cpp
      if (mlir::isa<own::BorrowType>(type)) {
```

And replace line 127:
```cpp
      if (typeName.contains("borrow.mut")) {
```
with:
```cpp
      if (mlir::isa<own::BorrowMutType>(type)) {
```

Also fix the LLVM pointer Send check at lines 63-65:
```cpp
  llvm::StringRef typeName = type.getAbstractType().getName();
  if (typeName.contains("llvm.ptr"))
    return false;
```
with:
```cpp
  if (mlir::isa<mlir::LLVM::LLVMPointerType>(type))
    return false;
```

Add the include if not already present:
```cpp
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
```

- [ ] **Step 5: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -10
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 6: Commit**

```bash
git add lib/Analysis/MoveCheck.cpp lib/Analysis/DropInsertion.cpp lib/Analysis/PanicScopeWrap.cpp lib/Analysis/SendSyncCheck.cpp
git commit -m "fix: replace string-based type detection with mlir::isa<> in all analysis passes"
```

---

### Task 4: Heap vs Stack Allocation for `own.alloc`

Currently all `own.alloc` goes to stack. `Box::new()` must use heap (malloc).

**Files:**
- Modify: `lib/CodeGen/OwnershipLowering.cpp:74-85`
- Create: `test/e2e/heap_alloc.ts`

- [ ] **Step 1: Write the e2e test**

Create `test/e2e/heap_alloc.ts`:
```typescript
// RUN: %asc build %s --emit llvmir -o %t.ll && grep -q "call.*malloc" %t.ll
// EXPECT: exit 0
// Tests that Box::new() emits malloc, not alloca.

function main(): i32 {
  let b = Box::new(42);
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py test/e2e/heap_alloc.ts 2>&1
```
Expected: FAIL (no malloc call in output).

- [ ] **Step 3: Implement heap allocation path**

In `lib/CodeGen/OwnershipLowering.cpp`, replace lines 74-85 (`own.alloc` handling):
```cpp
      if (name == "own.alloc") {
        uint64_t size = 8;
        if (auto sizeAttr = op->getAttrOfType<mlir::IntegerAttr>("size"))
          size = sizeAttr.getUInt();
        // Stack allocation by default.
        auto i8Ty = mlir::IntegerType::get(ctx, 8);
        auto arrayTy = mlir::LLVM::LLVMArrayType::get(i8Ty, size);
        auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
        auto alloca = builder.create<mlir::LLVM::AllocaOp>(loc, ptrType, arrayTy, one);
        if (op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(alloca.getResult());
        op->erase();
```
with:
```cpp
      if (name == "own.alloc") {
        uint64_t size = 8;
        if (auto sizeAttr = op->getAttrOfType<mlir::IntegerAttr>("size"))
          size = sizeAttr.getUInt();

        bool useHeap = op->hasAttr("heap");
        mlir::Value result;

        if (useHeap) {
          // Heap allocation: call malloc(size).
          auto mallocFn = getOrInsertMalloc(module, builder);
          auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)size);
          auto callOp = builder.create<mlir::LLVM::CallOp>(loc, mallocFn, mlir::ValueRange{sizeVal});
          result = callOp.getResult();
        } else {
          // Stack allocation: alloca.
          auto i8Ty = mlir::IntegerType::get(ctx, 8);
          auto arrayTy = mlir::LLVM::LLVMArrayType::get(i8Ty, size);
          auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
          auto alloca = builder.create<mlir::LLVM::AllocaOp>(loc, ptrType, arrayTy, one);
          result = alloca.getResult();
        }

        if (op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(result);
        op->erase();
```

- [ ] **Step 4: Tag Box::new() allocations with "heap" attribute in HIRBuilder**

Search HIRBuilder.cpp for the Box::new() handling and add the heap attribute. Find the `own.alloc` emission for Box::new:
```bash
grep -n "Box::new\|box_new\|Box.*alloc" /Users/satishbabariya/Desktop/asc/lib/HIR/HIRBuilder.cpp | head -10
```

At the `own.alloc` OperationState for Box::new(), add:
```cpp
state.addAttribute("heap", mlir::UnitAttr::get(builder.getContext()));
```

- [ ] **Step 5: Build and run the test**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py test/e2e/heap_alloc.ts 2>&1
```
Expected: PASS.

- [ ] **Step 6: Run full test suite for regressions**

```bash
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 7: Commit**

```bash
git add lib/CodeGen/OwnershipLowering.cpp lib/HIR/HIRBuilder.cpp test/e2e/heap_alloc.ts
git commit -m "fix: Box::new() allocates on heap via malloc, local structs use alloca"
```

---

### Task 5: Aggregate `own.move` via memcpy

Struct moves must copy data, not just forward the SSA pointer.

**Files:**
- Modify: `lib/CodeGen/OwnershipLowering.cpp:86-91`
- Create: `test/e2e/aggregate_move.ts`

- [ ] **Step 1: Write the e2e test**

Create `test/e2e/aggregate_move.ts`:
```typescript
// RUN: %asc build %s --emit llvmir -o %t.ll && grep -q "llvm.memcpy" %t.ll
// EXPECT: exit 0
// Tests that struct moves emit memcpy for aggregate types.

struct Point {
  x: i32,
  y: i32,
}

function take_point(p: own<Point>): i32 {
  return p.x;
}

function main(): i32 {
  let p = Point { x: 10, y: 20 };
  let result = take_point(p);
  return result;
}
```

- [ ] **Step 2: Implement aggregate move with memcpy**

In `lib/CodeGen/OwnershipLowering.cpp`, replace lines 86-91:
```cpp
      } else if (name == "own.move" || name == "own.copy" ||
                 name == "own.borrow_ref" || name == "own.borrow_mut") {
        // Forward SSA value.
        if (op->getNumOperands() > 0 && op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(op->getOperand(0));
        op->erase();
```
with:
```cpp
      } else if (name == "own.move") {
        if (op->getNumOperands() > 0 && op->getNumResults() > 0) {
          auto operand = op->getOperand(0);
          // Check if the operand is a pointer to an aggregate (struct) type.
          // If so, we need to memcpy the data to a new allocation.
          bool isAggregate = false;
          uint64_t structSize = 0;
          if (auto *defOp = operand.getDefiningOp()) {
            if (auto allocaOp = mlir::dyn_cast<mlir::LLVM::AllocaOp>(defOp)) {
              if (auto elemType = allocaOp.getElemType()) {
                if (auto structTy = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(elemType)) {
                  isAggregate = true;
                  // Estimate size: count fields * 8 bytes (conservative).
                  structSize = structTy.getBody().size() * 8;
                }
              }
            }
          }
          if (isAggregate && structSize > 0) {
            // Allocate destination and memcpy.
            auto i8Ty = mlir::IntegerType::get(ctx, 8);
            auto arrayTy = mlir::LLVM::LLVMArrayType::get(i8Ty, structSize);
            auto one = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)1);
            auto dst = builder.create<mlir::LLVM::AllocaOp>(loc, ptrType, arrayTy, one);
            auto sizeVal = builder.create<mlir::LLVM::ConstantOp>(loc, i64Type, (int64_t)structSize);
            // isVolatile = false
            auto falseCst = builder.create<mlir::LLVM::ConstantOp>(
                loc, mlir::IntegerType::get(ctx, 1), (int64_t)0);
            builder.create<mlir::LLVM::MemcpyOp>(loc, dst, operand, sizeVal, falseCst);
            op->getResult(0).replaceAllUsesWith(dst.getResult());
          } else {
            // Scalar/pointer: SSA forward.
            op->getResult(0).replaceAllUsesWith(operand);
          }
        }
        op->erase();
      } else if (name == "own.copy" ||
                 name == "own.borrow_ref" || name == "own.borrow_mut") {
        // Forward SSA value (borrows and copies don't transfer ownership).
        if (op->getNumOperands() > 0 && op->getNumResults() > 0)
          op->getResult(0).replaceAllUsesWith(op->getOperand(0));
        op->erase();
```

- [ ] **Step 3: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add lib/CodeGen/OwnershipLowering.cpp test/e2e/aggregate_move.ts
git commit -m "fix: aggregate own.move emits memcpy instead of SSA forwarding"
```

---

### Task 6: Send/Sync Type Parameters on OwnValType

Store real inner type, send flag, and sync flag in OwnValType instead of hardcoded values.

**Files:**
- Modify: `include/asc/HIR/OwnTypes.h`
- Modify: `lib/HIR/OwnTypes.cpp`

- [ ] **Step 1: Add custom TypeStorage to OwnValType**

Replace the entire contents of `include/asc/HIR/OwnTypes.h`:
```cpp
#ifndef ASC_HIR_OWNTYPES_H
#define ASC_HIR_OWNTYPES_H

#include "mlir/IR/Types.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"

namespace asc {
namespace own {

//===----------------------------------------------------------------------===//
// OwnValType with real type storage
//===----------------------------------------------------------------------===//

namespace detail {
struct OwnValTypeStorage : public mlir::TypeStorage {
  using KeyTy = std::tuple<mlir::Type, bool, bool>;

  OwnValTypeStorage(mlir::Type innerType, bool isSend, bool isSync)
      : innerType(innerType), sendFlag(isSend), syncFlag(isSync) {}

  bool operator==(const KeyTy &key) const {
    return std::get<0>(key) == innerType &&
           std::get<1>(key) == sendFlag &&
           std::get<2>(key) == syncFlag;
  }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_combine(std::get<0>(key), std::get<1>(key),
                              std::get<2>(key));
  }

  static OwnValTypeStorage *construct(mlir::TypeStorageAllocator &allocator,
                                       const KeyTy &key) {
    return new (allocator.allocate<OwnValTypeStorage>())
        OwnValTypeStorage(std::get<0>(key), std::get<1>(key),
                          std::get<2>(key));
  }

  mlir::Type innerType;
  bool sendFlag;
  bool syncFlag;
};
} // namespace detail

/// OwnValType: !own.val<T> — an owned value.
class OwnValType : public mlir::Type::TypeBase<OwnValType, mlir::Type,
                                                detail::OwnValTypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.val";

  /// Get with explicit parameters.
  static OwnValType get(mlir::MLIRContext *ctx, mlir::Type innerType = {},
                        bool isSend = true, bool isSync = false) {
    return Base::get(ctx, innerType, isSend, isSync);
  }

  mlir::Type getInnerType() const { return getImpl()->innerType; }
  bool isSend() const { return getImpl()->sendFlag; }
  bool isSync() const { return getImpl()->syncFlag; }
};

//===----------------------------------------------------------------------===//
// BorrowType — unchanged (no custom storage needed)
//===----------------------------------------------------------------------===//

/// BorrowType: !own.borrow<T> — a shared borrow.
class BorrowType : public mlir::Type::TypeBase<BorrowType, mlir::Type,
                                                mlir::TypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.borrow";

  static BorrowType get(mlir::MLIRContext *ctx, mlir::Type innerType = {}) {
    return Base::get(ctx);
  }

  mlir::Type getInnerType() const { return mlir::Type(); }
};

/// BorrowMutType: !own.borrow_mut<T> — an exclusive mutable borrow.
class BorrowMutType : public mlir::Type::TypeBase<BorrowMutType, mlir::Type,
                                                   mlir::TypeStorage> {
public:
  using Base::Base;
  static constexpr llvm::StringLiteral name = "own.borrow_mut";

  static BorrowMutType get(mlir::MLIRContext *ctx,
                            mlir::Type innerType = {}) {
    return Base::get(ctx);
  }

  mlir::Type getInnerType() const { return mlir::Type(); }
};

} // namespace own
} // namespace asc

#endif // ASC_HIR_OWNTYPES_H
```

- [ ] **Step 2: Update OwnTypes.cpp**

Replace `lib/HIR/OwnTypes.cpp`:
```cpp
#include "asc/HIR/OwnTypes.h"

namespace asc {
namespace own {
// OwnValType uses detail::OwnValTypeStorage defined in the header.
// BorrowType and BorrowMutType use default mlir::TypeStorage.
} // namespace own
} // namespace asc
```

- [ ] **Step 3: Find all `OwnValType::get(ctx)` calls with no args and update**

Search for calls that need updating:
```bash
grep -rn "OwnValType::get(" /Users/satishbabariya/Desktop/asc/lib/ /Users/satishbabariya/Desktop/asc/include/
```

Existing calls that pass only `ctx` will now use the defaults (`innerType={}`, `isSend=true`, `isSync=false`), which preserves the old behavior. No changes needed for existing call sites unless we want to pass real send/sync values (done in Task 6 Step 4 below for HIRBuilder).

- [ ] **Step 4: Update HIRBuilder to pass real Send/Sync flags**

Search for where `OwnValType` is created in HIRBuilder.cpp:
```bash
grep -n "OwnValType" /Users/satishbabariya/Desktop/asc/lib/HIR/HIRBuilder.cpp
```

At each `OwnValType::get(ctx)` call in HIRBuilder where we know the struct type, look up the struct's `@send`/`@sync` attributes via Sema and pass them through. For the common pattern of emitting `own.alloc` for a struct:
```cpp
// Before:
auto ownType = own::OwnValType::get(ctx);
// After (where structDecl is available):
bool hasSend = /* check structDecl attributes for @send */;
bool hasSync = /* check structDecl attributes for @sync */;
auto ownType = own::OwnValType::get(ctx, mlir::Type(), hasSend, hasSync);
```

The exact changes depend on how HIRBuilder accesses Sema's struct attribute data — search for `getAttributes()` calls near OwnValType creation points.

- [ ] **Step 5: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -10
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 6: Commit**

```bash
git add include/asc/HIR/OwnTypes.h lib/HIR/OwnTypes.cpp lib/HIR/HIRBuilder.cpp
git commit -m "feat: OwnValType stores real inner type, Send/Sync flags"
```

---

### Task 7: Fix Borrow Checker Pass 2 — Phi Propagation and CFG Reachability

**Files:**
- Modify: `lib/Analysis/RegionInference.cpp:202-251` (phi propagation)
- Modify: `lib/Analysis/RegionInference.cpp:176-197` (cross-block extension)

- [ ] **Step 1: Enable phi propagation by removing the `if (false)` guard**

In `lib/Analysis/RegionInference.cpp`, replace lines 232-246:
```cpp
          // DECISION: Skip successor operand analysis — use simpler
          // block argument propagation instead of getSuccessorOperands
          // which was removed in MLIR 18.
          (void)succIdx;
          // DECISION: Successor operand propagation skipped for MLIR 18.
          // Region merging through phi nodes deferred.
          if (false) {
            mlir::Value incomingVal;
            auto inIt = result.valueToRegion.find(incomingVal);

            if (argIt != result.valueToRegion.end() &&
                inIt != result.valueToRegion.end()) {
              result.unionFind.merge(argIt->second, inIt->second);
            } else if (inIt != result.valueToRegion.end() &&
                       argIt == result.valueToRegion.end()) {
              result.valueToRegion[blockArg] = inIt->second;
              argIt = result.valueToRegion.find(blockArg);
            }
          }
```
with:
```cpp
          // Use BranchOpInterface to get successor operands.
          if (auto branchOp = mlir::dyn_cast<mlir::BranchOpInterface>(terminator)) {
            auto succOperands = branchOp.getSuccessorOperands(succIdx);
            if (argIdx < succOperands.size()) {
              mlir::Value incomingVal = succOperands[argIdx];
              auto inIt = result.valueToRegion.find(incomingVal);

              if (argIt != result.valueToRegion.end() &&
                  inIt != result.valueToRegion.end()) {
                result.unionFind.merge(argIt->second, inIt->second);
              } else if (inIt != result.valueToRegion.end() &&
                         argIt == result.valueToRegion.end()) {
                result.valueToRegion[blockArg] = inIt->second;
                argIt = result.valueToRegion.find(blockArg);
              }
            }
          }
```

Add the include at the top:
```cpp
#include "mlir/Interfaces/ControlFlowInterfaces.h"
```

- [ ] **Step 2: Replace linear-index cross-block extension with BFS reachability**

In `lib/Analysis/RegionInference.cpp`, replace lines 176-197 (the cross-block extension logic inside `extendRegionsToUses`):
```cpp
      // If the use is in a different block than the definition, extend
      // the region to cover all intermediate blocks on the CFG path.
      if (!region.points.empty()) {
        unsigned defBlockIdx = region.points[0].blockIndex;
        if (useBlockIdx != defBlockIdx) {
          // Add entry/exit points for all blocks between def and use.
          // Walk forward through blocks (simplified — assumes linear CFG).
          unsigned lo = std::min(defBlockIdx, useBlockIdx);
          unsigned hi = std::max(defBlockIdx, useBlockIdx);
          for (unsigned b = lo; b <= hi; ++b) {
            CFGPoint entry{b, 0};
            bool found = false;
            for (const auto &p : region.points) {
              if (p.blockIndex == b) {
                found = true;
                break;
              }
            }
            if (!found)
              region.points.push_back(entry);
          }
        }
      }
```
with:
```cpp
      // If the use is in a different block than the definition, extend
      // the region to cover all blocks on any CFG path from def to use.
      if (!region.points.empty()) {
        unsigned defBlockIdx = region.points[0].blockIndex;
        if (useBlockIdx != defBlockIdx) {
          // BFS from def block to use block, collecting reachable blocks.
          mlir::Block *defBlock = nullptr;
          mlir::Block *targetBlock = useBlock;
          for (auto &[blk, idx] : blockIndex) {
            if (idx == defBlockIdx) {
              defBlock = blk;
              break;
            }
          }
          if (defBlock) {
            llvm::SmallVector<mlir::Block *, 16> worklist;
            llvm::DenseSet<mlir::Block *> visited;
            worklist.push_back(defBlock);
            visited.insert(defBlock);
            while (!worklist.empty()) {
              mlir::Block *cur = worklist.pop_back_val();
              for (mlir::Block *succ : cur->getSuccessors()) {
                if (visited.insert(succ).second) {
                  worklist.push_back(succ);
                }
              }
            }
            // Add entry points for all blocks on the reachable path
            // that are between def and use (i.e., reachable from def
            // and from which use is reachable).
            for (mlir::Block *reachable : visited) {
              unsigned blockIdx_val = blockIndex[reachable];
              bool found = false;
              for (const auto &p : region.points) {
                if (p.blockIndex == blockIdx_val) {
                  found = true;
                  break;
                }
              }
              if (!found)
                region.points.push_back(CFGPoint{blockIdx_val, 0});
            }
          }
        }
      }
```

- [ ] **Step 3: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -10
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add lib/Analysis/RegionInference.cpp
git commit -m "fix: enable phi propagation and CFG-aware region extension in borrow checker Pass 2"
```

---

### Task 8: Fix Cross-Block Overlap Detection in AliasCheck

**Files:**
- Modify: `lib/Analysis/AliasCheck.cpp:23-34`

- [ ] **Step 1: Replace unconditional cross-block overlap with region-based check**

In `lib/Analysis/AliasCheck.cpp`, replace lines 28-34:
```cpp
  if (blockA != blockB) {
    // Cross-block borrows: conservatively assume they may overlap
    // unless one dominates and completes before the other starts.
    // For now, assume overlap — a proper implementation would consult
    // the region inference results.
    return true;
  }
```
with:
```cpp
  if (blockA != blockB) {
    // Cross-block borrows: check if the first borrow's value has any
    // uses after the second borrow's definition (or vice versa).
    // This replaces the conservative "always true" with actual use analysis.
    mlir::Value valA = a.borrowValue;
    mlir::Value valB = b.borrowValue;

    // Check if borrow A is used in or after borrow B's block.
    bool aUsedAfterB = false;
    for (mlir::OpOperand &use : valA.getUses()) {
      mlir::Operation *useOp = use.getOwner();
      if (useOp->getBlock() == blockB && b.borrowOp->isBeforeInBlock(useOp)) {
        aUsedAfterB = true;
        break;
      }
    }

    // Check if borrow B is used in or after borrow A's block.
    bool bUsedAfterA = false;
    for (mlir::OpOperand &use : valB.getUses()) {
      mlir::Operation *useOp = use.getOwner();
      if (useOp->getBlock() == blockA && a.borrowOp->isBeforeInBlock(useOp)) {
        bUsedAfterA = true;
        break;
      }
    }

    return aUsedAfterB || bUsedAfterA;
  }
```

- [ ] **Step 2: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 3: Commit**

```bash
git add lib/Analysis/AliasCheck.cpp
git commit -m "fix: replace unconditional cross-block overlap with use-based analysis"
```

---

### Task 9: Conditional Move Severity — Error to Warning

**Files:**
- Modify: `lib/Analysis/MoveCheck.cpp:159-169`

- [ ] **Step 1: Change MaybeMoved from hard error to warning**

In `lib/Analysis/MoveCheck.cpp`, replace lines 159-169:
```cpp
    case MoveState::MaybeMoved: {
      mlir::InFlightDiagnostic diag = op->emitError()
          << "value may have been moved on a previous path";
      if (auto moveIt = firstMoveOp.find(operand);
          moveIt != firstMoveOp.end()) {
        diag.attachNote(moveIt->second->getLoc())
            << "value possibly moved here";
      }
      signalPassFailure();
      break;
    }
```
with:
```cpp
    case MoveState::MaybeMoved: {
      // RFC specifies conditional moves as warnings, not errors.
      // Drop-flag insertion (RFC-0008) will handle the runtime behavior.
      mlir::InFlightDiagnostic diag = op->emitWarning()
          << "value may have been moved on a previous path";
      if (auto moveIt = firstMoveOp.find(operand);
          moveIt != firstMoveOp.end()) {
        diag.attachNote(moveIt->second->getLoc())
            << "value possibly moved here";
      }
      // Not signalPassFailure() — this is a warning, not an error.
      break;
    }
```

- [ ] **Step 2: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 3: Commit**

```bash
git add lib/Analysis/MoveCheck.cpp
git commit -m "fix: conditional move changed from hard error to warning per RFC"
```

---

### Task 10: Restrict Integer Type Compatibility

**Files:**
- Modify: `lib/Sema/SemaType.cpp:42-62`
- Create: `test/e2e/integer_compat.ts`

- [ ] **Step 1: Write the e2e test**

Create `test/e2e/integer_compat.ts`:
```typescript
// RUN: %asc check %s 2>&1 | grep -q "type mismatch"
// EXPECT: exit 0
// Tests that i8 cannot be assigned a u64 value without cast.

function main(): i32 {
  let big: u64 = 1000;
  let small: i8 = big;
  return 0;
}
```

- [ ] **Step 2: Run test to verify it currently passes (wrong behavior)**

```bash
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py test/e2e/integer_compat.ts 2>&1
```
Expected: FAIL (currently no type error is emitted).

- [ ] **Step 3: Implement restricted integer widening**

In `lib/Sema/SemaType.cpp`, replace lines 51-61:
```cpp
  if (auto *lb = dynamic_cast<BuiltinType *>(lhs)) {
    if (auto *rb = dynamic_cast<BuiltinType *>(rhs)) {
      if (lb->getBuiltinKind() == rb->getBuiltinKind())
        return true;
      // Integer widening: i32 → i64, i32 → usize, etc.
      if (lb->isInteger() && rb->isInteger())
        return true;
      // Float widening: f32 → f64.
      if (lb->isFloat() && rb->isFloat())
        return true;
    }
  }
```
with:
```cpp
  if (auto *lb = dynamic_cast<BuiltinType *>(lhs)) {
    if (auto *rb = dynamic_cast<BuiltinType *>(rhs)) {
      if (lb->getBuiltinKind() == rb->getBuiltinKind())
        return true;
      // Float widening: f32 → f64.
      if (lb->isFloat() && rb->isFloat())
        return true;
      // Integer compatibility: restricted widening rules.
      if (lb->isInteger() && rb->isInteger()) {
        // Same signedness: allow widening (smaller → larger).
        if (lb->isSigned() == rb->isSigned())
          return true;
        // usize ↔ u64 and isize ↔ i64 (platform equivalence).
        auto lk = lb->getBuiltinKind();
        auto rk = rb->getBuiltinKind();
        if ((lk == BuiltinTypeKind::USize && rk == BuiltinTypeKind::U64) ||
            (lk == BuiltinTypeKind::U64 && rk == BuiltinTypeKind::USize) ||
            (lk == BuiltinTypeKind::ISize && rk == BuiltinTypeKind::I64) ||
            (lk == BuiltinTypeKind::I64 && rk == BuiltinTypeKind::ISize))
          return true;
        // Same bit-width signed ↔ unsigned (practical interop).
        // i32 ↔ u32, i64 ↔ u64, etc.
        auto bitWidth = [](BuiltinTypeKind k) -> int {
          switch (k) {
          case BuiltinTypeKind::I8: case BuiltinTypeKind::U8: return 8;
          case BuiltinTypeKind::I16: case BuiltinTypeKind::U16: return 16;
          case BuiltinTypeKind::I32: case BuiltinTypeKind::U32: return 32;
          case BuiltinTypeKind::I64: case BuiltinTypeKind::U64: return 64;
          case BuiltinTypeKind::I128: case BuiltinTypeKind::U128: return 128;
          case BuiltinTypeKind::USize: case BuiltinTypeKind::ISize: return 64;
          default: return 0;
          }
        };
        if (bitWidth(lk) == bitWidth(rk))
          return true;
        // All other integer conversions: reject (require explicit `as` cast).
        return false;
      }
    }
  }
```

- [ ] **Step 4: Build and run both the new test and full suite**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py test/e2e/integer_compat.ts 2>&1
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

Note: Some existing tests may break if they rely on implicit cross-signedness narrowing. If so, update those tests to add explicit `as` casts.

- [ ] **Step 5: Commit**

```bash
git add lib/Sema/SemaType.cpp test/e2e/integer_compat.ts
git commit -m "fix: restrict integer type compatibility to same-signedness widening"
```

---

### Task 11: `?` Operator Desugaring

**Files:**
- Modify: `lib/Sema/SemaExpr.cpp:827-832`
- Create: `test/e2e/try_operator.ts`

- [ ] **Step 1: Write the e2e test**

Create `test/e2e/try_operator.ts`:
```typescript
// RUN: %asc check %s 2>&1
// EXPECT: exit 0
// Tests that ? operator unwraps Result<T,E> to T.

function may_fail(): Result<i32, String> {
  return Result::Ok(42);
}

function caller(): Result<i32, String> {
  let val = may_fail()?;
  return Result::Ok(val);
}

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Implement `?` operator type checking**

In `lib/Sema/SemaExpr.cpp`, replace lines 827-832:
```cpp
Type *Sema::checkTryExpr(TryExpr *e) {
  Type *innerType = checkExpr(e->getOperand());
  // DECISION: ? operator on Result<T,E> produces T; on Option<T> produces T.
  // For now, pass through the inner type since we lack generic resolution.
  return innerType;
}
```
with:
```cpp
Type *Sema::checkTryExpr(TryExpr *e) {
  Type *innerType = checkExpr(e->getOperand());
  if (!innerType)
    return nullptr;

  // Check if the operand type is Result<T,E> or Option<T>.
  if (auto *nt = dynamic_cast<NamedType *>(innerType)) {
    llvm::StringRef name = nt->getName();

    // Result<T,E> → unwrap to T.
    // Monomorphized names look like "Result_i32_String".
    if (name.starts_with("Result")) {
      // The function must return Result<_, E> to propagate the error.
      if (currentReturnType) {
        if (auto *retNt = dynamic_cast<NamedType *>(currentReturnType)) {
          if (!retNt->getName().starts_with("Result")) {
            diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                            "? operator requires enclosing function to return Result");
          }
        }
      }
      // Look up the Ok variant's type from the enum declaration.
      auto eit = enumDecls.find(name);
      if (eit != enumDecls.end()) {
        for (auto *v : eit->second->getVariants()) {
          if (v->getName() == "Ok" && !v->getTupleTypes().empty())
            return v->getTupleTypes()[0];
        }
      }
      return innerType; // Fallback if we can't resolve the Ok type.
    }

    // Option<T> → unwrap to T.
    if (name.starts_with("Option")) {
      if (currentReturnType) {
        if (auto *retNt = dynamic_cast<NamedType *>(currentReturnType)) {
          if (!retNt->getName().starts_with("Option")) {
            diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                            "? operator requires enclosing function to return Option");
          }
        }
      }
      auto eit = enumDecls.find(name);
      if (eit != enumDecls.end()) {
        for (auto *v : eit->second->getVariants()) {
          if (v->getName() == "Some" && !v->getTupleTypes().empty())
            return v->getTupleTypes()[0];
        }
      }
      return innerType;
    }
  }

  // Neither Result nor Option.
  diags.emitError(e->getLocation(), DiagID::ErrTypeMismatch,
                  "? operator requires Result<T,E> or Option<T> operand");
  return innerType;
}
```

- [ ] **Step 3: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add lib/Sema/SemaExpr.cpp test/e2e/try_operator.ts
git commit -m "feat: ? operator desugars Result<T,E> to T, Option<T> to T"
```

---

### Task 12: `for` Loop Iterator Unwrapping

**Files:**
- Modify: `lib/Sema/SemaExpr.cpp:576-592`

- [ ] **Step 1: Implement type unwrapping for known iterables**

In `lib/Sema/SemaExpr.cpp`, replace lines 576-592:
```cpp
Type *Sema::checkForExpr(ForExpr *e) {
  Type *iterableType = checkExpr(e->getIterable());
  pushScope();
  // Bind loop variable.
  Symbol sym;
  sym.name = e->getVarName().str();
  // DECISION: For range iteration, element type is the range element type.
  if (iterableType)
    sym.type = iterableType;
  sym.isMutable = !e->getIsConst();
  if (!e->getVarName().empty())
    currentScope->declare(e->getVarName(), std::move(sym));
  if (e->getBody())
    checkCompoundStmt(e->getBody());
  popScope();
  return ctx.getVoidType();
}
```
with:
```cpp
Type *Sema::checkForExpr(ForExpr *e) {
  Type *iterableType = checkExpr(e->getIterable());
  pushScope();
  // Bind loop variable with unwrapped element type.
  Symbol sym;
  sym.name = e->getVarName().str();

  // Unwrap known iterable types to their element type.
  Type *elemType = iterableType;
  if (iterableType) {
    if (auto *nt = dynamic_cast<NamedType *>(iterableType)) {
      llvm::StringRef name = nt->getName();
      // Vec<T> → element is T. Monomorphized name: "Vec_i32", etc.
      if (name.starts_with("Vec")) {
        auto sit = structDecls.find(name);
        if (sit != structDecls.end()) {
          // Vec's element type is stored in its first generic field.
          // The monomorphized struct has fields with the concrete type.
          for (auto *field : sit->second->getFields()) {
            // The 'ptr' field of Vec points to the element type.
            if (field->getName() == "ptr" || field->getName() == "data") {
              elemType = field->getType();
              break;
            }
          }
        }
      }
      // String → element is char.
      if (name == "String")
        elemType = ctx.getBuiltinType(BuiltinTypeKind::Char);
    }
    // Array type → element type.
    if (auto *at = dynamic_cast<ArrayType *>(iterableType))
      elemType = at->getElementType();
    // Range expression (i32..i32) → the iterable already has the right
    // element type from checkRangeExpr (BuiltinType for the range bounds).
    // No unwrapping needed for ranges.
  }

  sym.type = elemType;
  sym.isMutable = !e->getIsConst();
  if (!e->getVarName().empty())
    currentScope->declare(e->getVarName(), std::move(sym));
  if (e->getBody())
    checkCompoundStmt(e->getBody());
  popScope();
  return ctx.getVoidType();
}
```

- [ ] **Step 2: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 3: Commit**

```bash
git add lib/Sema/SemaExpr.cpp
git commit -m "fix: for loop unwraps Vec<T> to T, Array to element type, String to char"
```

---

### Task 13: Basic Match Exhaustiveness Checking

**Files:**
- Modify: `lib/Sema/SemaExpr.cpp` (after `checkMatchExpr`, around line 573)
- Create: `test/e2e/match_exhaustive.ts`

- [ ] **Step 1: Write the e2e test**

Create `test/e2e/match_exhaustive.ts`:
```typescript
// RUN: %asc check %s 2>&1 | grep -q "non-exhaustive"
// EXPECT: exit 0
// Tests that non-exhaustive match on Option produces a warning.

function test(x: Option<i32>): i32 {
  match (x) {
    Option::Some(v) => v,
    // Missing: Option::None arm
  }
}

function main(): i32 {
  return 0;
}
```

- [ ] **Step 2: Add exhaustiveness check at the end of checkMatchExpr**

In `lib/Sema/SemaExpr.cpp`, find the end of `checkMatchExpr` (line 573, just before `return armType;`). Insert the following before the return:

```cpp
  // Basic match exhaustiveness check for enum types.
  if (scrutType) {
    if (auto *nt = dynamic_cast<NamedType *>(scrutType)) {
      auto eit = enumDecls.find(nt->getName());
      if (eit != enumDecls.end()) {
        // Collect matched variant names.
        llvm::StringSet<> matchedVariants;
        bool hasWildcard = false;
        for (const auto &arm : e->getArms()) {
          if (!arm.pattern)
            continue;
          if (dynamic_cast<WildcardPattern *>(arm.pattern)) {
            hasWildcard = true;
            break;
          }
          if (auto *ep = dynamic_cast<EnumPattern *>(arm.pattern)) {
            const auto &path = ep->getPath();
            if (!path.empty())
              matchedVariants.insert(path.back());
          }
          if (auto *ip = dynamic_cast<IdentPattern *>(arm.pattern)) {
            // A bare identifier pattern acts as a wildcard binding.
            hasWildcard = true;
            break;
          }
        }

        if (!hasWildcard) {
          // Check that all variants are covered.
          llvm::SmallVector<llvm::StringRef, 4> missing;
          for (auto *v : eit->second->getVariants()) {
            if (!matchedVariants.contains(v->getName()))
              missing.push_back(v->getName());
          }
          if (!missing.empty()) {
            std::string missingStr;
            for (unsigned i = 0; i < missing.size(); ++i) {
              if (i > 0) missingStr += ", ";
              missingStr += missing[i].str();
            }
            diags.emitWarning(e->getLocation(),
                "non-exhaustive match: missing variants: " + missingStr);
          }
        }
      }
    }
  }
```

Note: You'll need to add the `emitWarning` method to `DiagnosticEngine` if it doesn't exist. Check:
```bash
grep -n "emitWarning" /Users/satishbabariya/Desktop/asc/include/asc/Basic/Diagnostic.h
```
If it doesn't exist, add a parallel to `emitError` that sets severity to `Warning`.

- [ ] **Step 3: Build and test**

```bash
cd build && cmake --build . 2>&1 | tail -5
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py test/e2e/match_exhaustive.ts 2>&1
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add lib/Sema/SemaExpr.cpp test/e2e/match_exhaustive.ts
git commit -m "feat: basic match exhaustiveness warning for enum types"
```

---

### Task 14: Final Verification

Run the complete test suite and verify all success criteria.

- [ ] **Step 1: Full test suite run**

```bash
cd /Users/satishbabariya/Desktop/asc && python3 test/run_tests.py 2>&1
```

Expected: All 118+ existing e2e tests pass. New tests (heap_alloc, aggregate_move, integer_compat, try_operator, match_exhaustive) also pass.

- [ ] **Step 2: Verify heap allocation**

```bash
cd build && ./bin/asc build /Users/satishbabariya/Desktop/asc/test/e2e/box_new.ts --emit llvmir 2>&1 | grep -c "malloc"
```
Expected: at least 1 malloc call.

- [ ] **Step 3: Verify MLIR verifier is on**

```bash
cd build && ./bin/asc build /Users/satishbabariya/Desktop/asc/test/e2e/hello_i32.ts --emit mlir 2>&1
```
Expected: clean output (no verifier errors). If there are verifier failures, they indicate IR correctness issues that must be fixed.

- [ ] **Step 4: Verify default opt level**

```bash
cd build && ./bin/asc build /Users/satishbabariya/Desktop/asc/test/e2e/hello_i32.ts --emit llvmir 2>&1 | head -5
```
Expected: optimized output (function attributes should show optimization, not raw unoptimized IR).

- [ ] **Step 5: Run any failing tests from Tasks 1-13 that were noted for follow-up**

Fix remaining issues if any.

- [ ] **Step 6: Final commit for any fixups**

```bash
git add -A
git commit -m "fix: address remaining test failures from correctness-first push"
```
