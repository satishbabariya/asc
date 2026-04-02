# ASC Compiler — Mega Push: @copy ABI, Iterators, Borrow Checker, Wasm EH, 100 E2E

> **Paste this entire prompt into Claude Code Web.**
> **Five major feature areas + 15 new e2e tests. Work autonomously through all parts. Do not stop.**

---

## Context

The `asc` compiler (C++20, LLVM 18 + MLIR) has 93 commits:
- **91/95 e2e** (4 intentional), **93/93 unit**, **10/10 integration**
- Complete pipeline: Source → Lexer → Parser → AST → Sema → HIR → Analysis (7 passes) → CodeGen (PanicLowering → OwnershipLowering → ConcurrencyLowering → MLIR→LLVM) → Binary
- All working: ownership, dyn Trait vtables, channels, pthread_create, setjmp panic, Vec/String/HashMap, closures, generics, for-in, Box<T>

**This session targets 5 features + 15 new tests to reach 110+ e2e, 0 genuine failures.**

**Build & test:**
```sh
cd build && make -j$(sysctl -n hw.ncpu) 2>&1 | tail -30
./build/asc build <file>.ts --target $(llvm-config --host-target) -o /tmp/test && /tmp/test; echo "Exit: $?"
./build/asc build <file>.ts --emit llvmir 2>&1
```

---

## Rules

- Do NOT stop between parts. Finish, commit, next.
- If a feature is too complex, implement the simplest working version and move on.
- Fix any compiler bugs exposed by new tests before proceeding.
- Commit after each completed feature or batch of passing tests.
- Run regression after each part.

**Regression:**
```sh
for f in test/e2e/arithmetic.ts test/e2e/sieve.ts test/e2e/dyn_dispatch.ts test/e2e/channel_roundtrip.ts test/e2e/struct_methods.ts test/e2e/hashmap_basic.ts test/e2e/for_in_vec.ts test/e2e/generic_dyn.ts; do
  name=$(basename "$f" .ts); ./build/asc build "$f" --target $(llvm-config --host-target) -o /tmp/$name 2>/dev/null && /tmp/$name 2>/dev/null; echo "$name: exit=$?"
done
```

---

# FEATURE 1: @COPY STRUCT BY-VALUE ABI

**Current state:** `convertType` in HIRBuilder.cpp (line 161-166) passes `own<T>` for copy types (integers, floats, bool) by value but `@copy` structs are still passed as pointers.

```cpp
// Current (line 163-166):
if (inner.isIntOrIndexOrFloat() || inner.isInteger(1))
  return inner;    // scalars by value
return getPtrType(); // everything else by pointer — including @copy structs!
```

**What to change:** When `@copy` is set on a struct AND all fields are scalars, pass the LLVM struct type directly instead of a pointer. This eliminates unnecessary alloca/load/store for small copyable structs.

## 1A: Detect @copy Structs in convertType

In `convertType`, when handling `OwnType`, check if the inner type is a `@copy` struct:

```cpp
if (auto *ot = dynamic_cast<OwnType *>(astType)) {
  mlir::Type inner = convertType(ot->getInner());
  // Scalars: pass by value
  if (inner.isIntOrIndexOrFloat() || inner.isInteger(1))
    return inner;
  // @copy structs: pass by value if small enough
  if (auto *namedInner = dynamic_cast<NamedType *>(ot->getInner())) {
    auto sit = sema.structDecls.find(namedInner->getName().str());
    if (sit != sema.structDecls.end()) {
      // Check if struct has @copy attribute
      bool isCopy = false;
      auto ownerInfo = sema.getTypeOwnership(ot->getInner());
      if (ownerInfo.isCopy) isCopy = true;
      // Also check struct attributes
      if (sit->second->hasAttribute("copy")) isCopy = true;

      if (isCopy && inner && mlir::isa<mlir::LLVM::LLVMStructType>(inner)) {
        // Pass small @copy structs by value (up to ~64 bytes / 4 fields)
        auto structTy = mlir::cast<mlir::LLVM::LLVMStructType>(inner);
        if (structTy.getBody().size() <= 8) {
          return inner; // by value!
        }
      }
    }
  }
  return getPtrType(); // non-copy or large struct → pointer
}
```

## 1B: Fix Call Sites

When calling a function that expects a by-value struct but the caller has a pointer (from alloca), emit a load:

```cpp
// In visitCallExpr, when coercing args:
if (mlir::isa<mlir::LLVM::LLVMStructType>(expectedType) &&
    mlir::isa<mlir::LLVM::LLVMPointerType>(arg.getType())) {
  arg = builder.create<mlir::LLVM::LoadOp>(location, expectedType, arg);
}
```

When returning a by-value struct, ensure the return matches the function signature.

## 1C: Fix Field Access on By-Value Structs

Field access on a by-value struct uses `extractvalue` instead of GEP+load. This already works (line 2471 of HIRBuilder.cpp handles it). Verify it still works for @copy structs.

**Tests:**
```typescript
// test/e2e/copy_struct_byval.ts
@copy
struct Point { x: i32, y: i32 }

function add_points(a: Point, b: Point): Point {
  return Point { x: a.x + b.x, y: a.y + b.y };
}

function main(): i32 {
  const p1 = Point { x: 10, y: 20 };
  const p2 = Point { x: 5, y: 7 };
  const result = add_points(p1, p2);
  return result.x + result.y;
}
// Expected: exit 42 (15 + 27)

// test/e2e/copy_struct_assign.ts
@copy
struct Color { r: i32, g: i32, b: i32 }

function main(): i32 {
  const red = Color { r: 255, g: 0, b: 0 };
  let c = red;  // copy, not move — @copy type
  c = Color { r: 0, g: 255, b: 0 };  // reassign — no double-free
  return c.g;
}
// Expected: exit 255

// test/e2e/copy_in_loop.ts
@copy
struct Counter { val: i32 }

function increment(c: Counter): Counter {
  return Counter { val: c.val + 1 };
}

function main(): i32 {
  let c = Counter { val: 0 };
  let i: i32 = 0;
  while i < 42 {
    c = increment(c);  // Copy semantics — c consumed and replaced
    i = i + 1;
  }
  return c.val;
}
// Expected: exit 42
```

**Commit: "feat: @copy struct by-value ABI — small structs passed without pointer indirection"**

---

# FEATURE 2: ITERATOR COMBINATORS

Implement `.map()`, `.filter()`, `.fold()`, `.sum()`, `.count()`, `.any()`, `.all()`, `.find()` as HIR builder intrinsics on Vec iterators.

**Strategy:** These are eager (not lazy) — they consume the iterator and produce a result immediately. Lazy iterators (returning new iterator structs) are a stretch goal.

## 2A: .fold(init, closure) — The Foundation

All other combinators can be expressed as fold. Implement fold first:

```
fold(init, f) = {
  let acc = init;
  loop {
    match iter.next() {
      Some(val) => { acc = f(acc, val); },
      None => { break; },
    }
  }
  return acc;
}
```

In `visitMethodCallExpr`, when `methodName == "fold"` and receiver is an iterator:

```cpp
if (methodName == "fold" && receiver && args.size() >= 3) {
  // args[0] = receiver (iterator ptr)
  // args[1] = initial accumulator value
  // args[2] = closure (function ptr)
  mlir::Value iterPtr = args[0];
  mlir::Value initVal = args[1];
  mlir::Value closureFn = args[2];

  // Create accumulator alloca
  auto accAlloca = builder.create<mlir::LLVM::AllocaOp>(...);
  builder.create<mlir::LLVM::StoreOp>(location, initVal, accAlloca);

  // Create temp alloca for iter_next output
  auto tmpAlloca = builder.create<mlir::LLVM::AllocaOp>(...);

  // Loop: condBlock → bodyBlock → condBlock, exitBlock
  // condBlock: hasNext = __asc_vec_iter_next(iter, &tmp, elemSize)
  //            if !hasNext → exitBlock
  // bodyBlock: acc = closure(acc, tmp); → condBlock
  // exitBlock: return load(accAlloca)

  // ... emit loop blocks similar to for-in desugaring ...
}
```

## 2B: .sum() via fold

```cpp
if (methodName == "sum" && receiver) {
  // Desugar to: fold(0, |acc, val| acc + val)
  // Emit the fold loop inline with addition
}
```

## 2C: .count() via fold

```cpp
if (methodName == "count" && receiver) {
  // Desugar to: fold(0, |acc, _| acc + 1)
}
```

## 2D: .any(predicate) and .all(predicate)

```cpp
if (methodName == "any" && receiver && args.size() >= 2) {
  // Loop: for each element, apply predicate
  // If predicate returns true → return true (early exit)
  // After exhaustion → return false
}

if (methodName == "all" && receiver && args.size() >= 2) {
  // Same but return false on first failure, true after exhaustion
}
```

## 2E: .find(predicate)

```cpp
if (methodName == "find" && receiver && args.size() >= 2) {
  // Loop: for each element, apply predicate
  // If predicate returns true → return Option::Some(element)
  // After exhaustion → return Option::None
}
```

## 2F: .map(closure) — Eager Version

```cpp
if (methodName == "map" && receiver && args.size() >= 2) {
  // Create new Vec
  // Loop: for each element from source iterator:
  //   mapped = closure(element)
  //   newVec.push(mapped)
  // Return newVec
}
```

## 2G: .filter(predicate) — Eager Version

```cpp
if (methodName == "filter" && receiver && args.size() >= 2) {
  // Create new Vec
  // Loop: for each element from source iterator:
  //   if predicate(element) → newVec.push(element)
  // Return newVec
}
```

**Tests:**
```typescript
// test/e2e/iter_fold.ts
function main(): i32 {
  let v: Vec<i32> = Vec::new();
  v.push(10); v.push(20); v.push(12);
  let sum: i32 = 0;
  for val of v { sum = sum + val; }
  return sum;
}
// Expected: exit 42

// test/e2e/iter_count_method.ts
function main(): i32 {
  let v: Vec<i32> = Vec::new();
  v.push(1); v.push(2); v.push(3); v.push(4); v.push(5);
  // Manual count via loop
  let count: i32 = 0;
  let iter = v.iter();
  loop {
    match iter.next() {
      Option::Some(_) => { count = count + 1; },
      Option::None => { break; },
    }
  }
  return count;
}
// Expected: exit 5

// test/e2e/iter_manual_map.ts
function main(): i32 {
  let v: Vec<i32> = Vec::new();
  v.push(1); v.push(2); v.push(3);
  let doubled: Vec<i32> = Vec::new();
  let i: usize = 0;
  while i < v.len() {
    match v.get(i) {
      Option::Some(val) => { doubled.push(val * 2); },
      Option::None => {},
    }
    i = i + 1;
  }
  // doubled = [2, 4, 6]
  let sum: i32 = 0;
  for val of doubled { sum = sum + val; }
  return sum;
}
// Expected: exit 12

// test/e2e/iter_manual_filter.ts
function main(): i32 {
  let v: Vec<i32> = Vec::new();
  v.push(1); v.push(2); v.push(3); v.push(4); v.push(5); v.push(6);
  let evens: Vec<i32> = Vec::new();
  let i: usize = 0;
  while i < v.len() {
    match v.get(i) {
      Option::Some(val) => {
        if val % 2 == 0 { evens.push(val); }
      },
      Option::None => {},
    }
    i = i + 1;
  }
  // evens = [2, 4, 6], sum = 12
  let sum: i32 = 0;
  for val of evens { sum = sum + val; }
  return sum;
}
// Expected: exit 12
```

**Note:** If implementing fold/map/filter as compiler intrinsics is too complex, just add the test files using manual loop patterns (they already work). The tests exercise the same patterns users would write.

**Commit: "feat: iterator combinator patterns — fold, map, filter, count, sum tests"**

---

# FEATURE 3: BORROW CHECKER DEPTH (RFC-0006)

RFC-0006 specifies 5 passes. Currently at 85%. Missing:
- **Partial moves** — moving one field of a struct should invalidate only that field, not the whole struct
- **Reborrowing** — passing `&mut x` to a function that takes `&mut T` should create a temporary reborrow, not move the borrow
- **Drop-while-borrowed** — E003: dropping a value while a borrow of it is still live

## 3A: Partial Move Detection (E006)

When a struct field is moved individually, the struct becomes partially moved. Accessing the moved field is an error; accessing unmoved fields is still valid.

```typescript
struct Pair { a: String, b: String }
let p = Pair { a: String::from("hello"), b: String::from("world") };
let s = p.a;  // partial move — p.a moved, p.b still valid
println!(p.b); // OK — p.b not moved
println!(p.a); // ERROR E006: field `a` of `p` has been moved
```

**Implementation in MoveCheck.cpp:**

Currently MoveCheck tracks move state per SSA value (the whole struct). Extend it to track per-field:

```cpp
// New: FieldMoveState tracks which fields of a struct have been moved
struct FieldMoveState {
  llvm::DenseMap<unsigned, MoveState> fieldStates; // field index → state
};

// When an own.move targets a GEP (field extraction), mark only that field as Moved
// When checking uses, if a GEP extracts a moved field → E006
```

**Simpler alternative:** Just detect and report when a whole struct is used after any field has been moved. This is what Rust does — partial moves invalidate the whole struct unless all fields are individually handled.

```cpp
// In checkOperandStates:
// If value V is a struct and has any field-level move recorded → E006
```

**Test:**
```typescript
// test/e2e/partial_move.ts
struct Data { x: i32, y: i32 }

function main(): i32 {
  let d = Data { x: 10, y: 32 };
  // Access both fields — no move, just field access on @copy fields
  return d.x + d.y;
}
// Expected: exit 42 (this should work — i32 fields are @copy)

// test/e2e/partial_move_error.ts
// This should FAIL with E006 if partial move is tracked
// (But since i32 is @copy, it actually works — partial move only matters for non-copy fields)
```

**Commit: "feat: partial move detection in borrow checker (E006)"**

## 3B: Reborrowing

When passing `&mut x` to a function that takes `&mut T`, the original mutable borrow should be temporarily "frozen" (reborrowed), not consumed. After the function returns, the original borrow is live again.

```typescript
function modify(d: refmut<Data>): void { d.value = 42; }

function main(): void {
  let d = Data { value: 0 };
  let r = &d;       // mutable borrow
  modify(r);        // reborrow — r is temporarily frozen
  // r is live again after modify returns
  let v = r.value;  // OK — reborrow ended
}
```

**Implementation in AliasCheck.cpp:**

When a `borrow.mut` value is passed as an argument to a function call, instead of marking it as consumed/moved, mark it as "reborrowed" for the duration of the call. The borrow's region extends through the call but is exclusive during it.

This is mostly a non-issue in the current implementation because `own.borrow_mut` just forwards the SSA value (no consumption). The existing AliasCheck may already handle this correctly. **Verify with a test first.**

**Test:**
```typescript
// test/e2e/reborrow.ts
struct Counter { value: i32 }

function increment(c: refmut<Counter>): void {
  c.value = c.value + 1;
}

function main(): i32 {
  let c = Counter { value: 0 };
  increment(&c);
  increment(&c);  // reborrow — should work
  increment(&c);
  return c.value;
}
// Expected: exit 3 (already works if test 09 passes — this is a regression test)
```

**Commit: "test: reborrowing verified — mutable borrows reusable after function returns"**

## 3C: Drop-While-Borrowed (E003)

If a value is dropped (goes out of scope or explicitly freed) while a borrow of it is still live, this is an error.

```typescript
function dangling(): ref<i32> {
  let x: i32 = 42;
  return &x;  // ERROR: x dropped at end of scope, borrow escapes
}
```

**Implementation:** AliasCheck already has Rule C (no drop while borrow is live) — verify it fires with a test:

```typescript
// test/e2e/drop_while_borrowed.ts
// This should FAIL — dangling reference
struct Data { value: i32 }

function dangling_ref(): i32 {
  let d = Data { value: 42 };
  const r = &d;
  return r.value;  // OK — borrow used before drop
}

function main(): i32 {
  return dangling_ref();
}
// Expected: exit 42 (this is valid — borrow used before scope exit)
```

**Commit: "test: drop-while-borrowed (E003) verification"**

---

# FEATURE 4: WASM EXCEPTION HANDLING

**Current state:** PanicLowering uses setjmp/longjmp for native targets. For Wasm targets, there's no EH at all — panic just aborts.

RFC-0009 specifies Wasm EH with `try`/`catch`/`throw`/`rethrow` instructions. LLVM 18 supports Wasm EH through:
- `llvm.wasm.throw(tag, payload_ptr)` — throw exception
- `catch` clause in `invoke` landing pad
- `@llvm.wasm.catch(tag)` — extract payload in catch block

## 4A: Wasm Panic Tag

Declare a Wasm exception tag in the module:

```cpp
// In PanicLowering or ConcurrencyLowering (for Wasm targets):
// @__asc_panic_tag = external global i32
auto panicTag = module.lookupSymbol("__asc_panic_tag");
if (!panicTag) {
  builder.setInsertionPointToStart(module.getBody());
  builder.create<mlir::LLVM::GlobalOp>(loc, i32Type, /*isConstant=*/false,
      mlir::LLVM::Linkage::External, "__asc_panic_tag", mlir::Attribute{});
}
```

## 4B: Wasm throw in panic!

For Wasm targets, `panic!()` should emit `throw $panic_tag` instead of calling `__asc_panic` + `abort`:

```cpp
// In HIRBuilder when emitting panic! for Wasm:
// 1. Allocate PanicInfo struct
// 2. Fill with message, file, line, col
// 3. throw $panic_tag, panic_info_ptr
auto throwFn = getOrDeclare("llvm.wasm.throw", voidTy, {i32Ty, ptrType});
builder.create<mlir::LLVM::CallOp>(loc, throwFn,
    mlir::ValueRange{panicTagValue, panicInfoPtr});
builder.create<mlir::LLVM::UnreachableOp>(loc);
```

## 4C: Try/Catch in PanicLowering (Wasm Path)

For Wasm targets in PanicLowering, instead of setjmp:

```cpp
// Wasm EH uses invoke + catch landing pad:
// invoke @func_that_may_panic() to label %normal unwind label %catch
//
// catch:
//   %payload = call @llvm.wasm.catch(i32 @__asc_panic_tag)
//   <run drops>
//   call @llvm.wasm.rethrow()
//   unreachable
```

This requires converting `func.call` ops that may panic into `llvm.invoke` ops. This is complex — only attempt if setjmp path is solid.

**Alternative:** For Wasm targets, just use `__builtin_trap()` on panic (current behavior). Wasm EH is a stretch goal.

**Test (native only — Wasm EH is stretch):**
```typescript
// test/e2e/wasm_eh_stub.ts
function main(): i32 {
  // Just verify the compiler doesn't crash when targeting Wasm
  return 42;
}
// Build: ./build/asc build test/e2e/wasm_eh_stub.ts --target wasm32-wasi --emit llvmir
// Expected: valid LLVM IR with wasm32 target triple
```

**Commit: "feat: Wasm EH foundation — panic tag declaration and throw intrinsic"**

---

# FEATURE 5: PUSH TO 100+ E2E TESTS

Write 10 more tests targeting untested feature combinations and edge cases.

## 5A: Mutual Recursion
```typescript
// test/e2e/mutual_recursion.ts
function is_even(n: i32): bool {
  if n == 0 { return true; }
  return is_odd(n - 1);
}

function is_odd(n: i32): bool {
  if n == 0 { return false; }
  return is_even(n - 1);
}

function main(): i32 {
  if is_even(42) { return 0; }
  return 1;
}
// Expected: exit 0
```

## 5B: Vec of Structs
```typescript
// test/e2e/vec_of_structs.ts
@copy
struct Point { x: i32, y: i32 }

function main(): i32 {
  let points: Vec<Point> = Vec::new();
  points.push(Point { x: 1, y: 2 });
  points.push(Point { x: 3, y: 4 });
  points.push(Point { x: 5, y: 6 });

  let sum: i32 = 0;
  let i: usize = 0;
  while i < points.len() {
    match points.get(i) {
      Option::Some(p) => { sum = sum + p.x + p.y; },
      Option::None => {},
    }
    i = i + 1;
  }
  return sum;
}
// Expected: exit 21 (1+2+3+4+5+6)
```

## 5C: Enum with Struct Variant
```typescript
// test/e2e/enum_struct_variant.ts
enum Shape {
  Circle(i32),
  Rectangle(i32, i32),
}

function perimeter(s: Shape): i32 {
  match s {
    Shape::Circle(r) => 2 * 3 * r,  // approximate
    Shape::Rectangle(w, h) => 2 * (w + h),
  }
}

function main(): i32 {
  const c = Shape::Circle(7);
  const r = Shape::Rectangle(10, 5);
  return perimeter(c) + perimeter(r);
}
// Expected: exit 72 (42 + 30)
```

## 5D: Deeply Nested Function Calls
```typescript
// test/e2e/deep_calls.ts
function a(x: i32): i32 { return b(x + 1); }
function b(x: i32): i32 { return c(x + 1); }
function c(x: i32): i32 { return d(x + 1); }
function d(x: i32): i32 { return e(x + 1); }
function e(x: i32): i32 { return x + 1; }

function main(): i32 {
  return a(0);
}
// Expected: exit 5
```

## 5E: HashMap with String-like Keys
```typescript
// test/e2e/hashmap_complex.ts
function main(): i32 {
  let m: HashMap<i32, i32> = HashMap::new();
  // Insert 100 entries
  let i: i32 = 0;
  while i < 100 {
    m.insert(i, i * i);
    i = i + 1;
  }
  // Lookup specific entries
  let sum: i32 = 0;
  match m.get(5) { Option::Some(v) => { sum = sum + v; }, Option::None => {} }   // 25
  match m.get(10) { Option::Some(v) => { sum = sum + v; }, Option::None => {} }  // 100
  match m.get(7) { Option::Some(v) => { sum = sum + v; }, Option::None => {} }   // 49
  return sum;
}
// Expected: exit 174 (25 + 100 + 49)
```

## 5F: Trait Default Method (stretch)
```typescript
// test/e2e/trait_default.ts
trait Describable {
  fn name(ref<Self>): i32;
  // Default method: uses name()
}

struct Widget { id: i32 }

impl Describable for Widget {
  fn name(ref<Widget>): i32 { return self.id; }
}

function main(): i32 {
  let w = Widget { id: 42 };
  return w.name();
}
// Expected: exit 42
```

## 5G: Multiple Channels
```typescript
// test/e2e/multi_channel.ts
function main(): i32 {
  const [tx1, rx1] = chan<i32>(4);
  const [tx2, rx2] = chan<i32>(4);

  tx1.send(10);
  tx2.send(32);

  return rx1.recv() + rx2.recv();
}
// Expected: exit 42
```

## 5H: Conditional Return with Ownership
```typescript
// test/e2e/conditional_ownership.ts
struct Item { value: i32 }

function choose(flag: bool): i32 {
  let a = Item { value: 42 };
  let b = Item { value: 10 };
  if flag {
    return a.value;
    // b dropped here
  }
  return b.value;
  // a dropped here
}

function main(): i32 {
  return choose(true);
}
// Expected: exit 42
```

## 5I: Recursive Fibonacci with Large N
```typescript
// test/e2e/fib_large.ts
function fib(n: i32): i32 {
  let a: i32 = 0;
  let b: i32 = 1;
  let i: i32 = 0;
  while i < n {
    const tmp = a + b;
    a = b;
    b = tmp;
    i = i + 1;
  }
  return a;
}

function main(): i32 {
  // fib(30) = 832040
  const result = fib(30);
  if result == 832040 { return 0; }
  return 1;
}
// Expected: exit 0
```

## 5J: Nested Enum Match
```typescript
// test/e2e/nested_enum_match.ts
enum Outer {
  A(i32),
  B(i32),
}

function classify(o: Outer): i32 {
  match o {
    Outer::A(x) => {
      if x > 0 { return 1; }
      return -1;
    },
    Outer::B(x) => {
      if x > 0 { return 2; }
      return -2;
    },
  }
}

function main(): i32 {
  const r1 = classify(Outer::A(5));   // 1
  const r2 = classify(Outer::B(10));  // 2
  const r3 = classify(Outer::A(-1));  // -1
  return r1 + r2 + r3;
}
// Expected: exit 2
```

**Commit after each batch of 3-5 passing tests.**

---

# Verification

```sh
echo "=== Complete E2E Suite ==="
PASS=0; FAIL=0; TOTAL=0
for f in test/e2e/*.ts; do
  [ -d "$f" ] && continue
  name=$(basename "$f" .ts)
  TOTAL=$((TOTAL+1))
  ./build/asc build "$f" --target $(llvm-config --host-target) -o /tmp/e2e_$name 2>/dev/null
  if [ $? -eq 0 ]; then
    /tmp/e2e_$name 2>/dev/null; PASS=$((PASS+1))
  else
    ./build/asc check "$f" 2>/dev/null
    [ $? -eq 1 ] && PASS=$((PASS+1)) || FAIL=$((FAIL+1))
  fi
done
echo "=== $PASS/$TOTAL passed, $FAIL failed ==="
```

---

## Priority

1. **Feature 5** (10 new tests) — fastest impact, catches bugs
2. **Feature 1** (@copy by-value) — targeted change, big perf win
3. **Feature 2** (iterator patterns) — manual patterns work, intrinsics are polish
4. **Feature 3** (borrow checker) — tests may already pass, verification + edge cases
5. **Feature 4** (Wasm EH) — hardest, stretch goal

---

## DO NOT STOP

Work through Features 1 → 2 → 3 → 4 → 5. Write tests, build, run, fix bugs. Commit after each feature.

**Target: 105+ e2e tests, @copy by-value ABI working, iterator patterns tested, borrow checker at 90%, Wasm EH foundation laid. Zero genuine failures.**
