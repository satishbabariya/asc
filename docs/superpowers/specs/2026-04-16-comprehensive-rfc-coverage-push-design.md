# Comprehensive RFC Coverage Push (74% → ~82%)

| Field | Value |
|---|---|
| Date | 2026-04-16 |
| Goal | Fix correctness bugs, push 5 core RFCs to ~90%+, raise overall weighted coverage from 74% to ~82% |
| Baseline | 245/245 tests passing, 74% weighted RFC coverage (per fresh audit) |
| Target | RFC-0004: 86%, RFC-0010: 80%, RFC-0011: 93%, RFC-0013: 90%, RFC-0014: 86% |

## Motivation

A fresh RFC-by-RFC audit (2026-04-16) against all 20 RFCs revealed the actual weighted coverage is ~74%, not the ~84% previously claimed. Key findings:

1. **Dead flags** — `--max-threads` and `--no-panic-unwind` are parsed in Driver.cpp:113-116 but never copied to `CodeGenOptions` at line 1092. They have zero runtime effect.
2. **Operator trait signature mismatch** — Builtins.cpp registers Add/Sub/Mul/Div with `ref<Self>` receiver, but `std/core/ops.ts` uses `own<Self>`.
3. **Iterator adapter structs exist but aren't trait methods** — Map, Filter, Take, Skip, etc. are standalone structs in `iter.ts`, not accessible as `.map()`, `.filter()` on Iterator.
4. **Missing atomic types** — Only AtomicI32/U32/I64/Bool; no AtomicU64/AtomicUsize/AtomicPtr (CLAUDE.md known gap #10).
5. **HashMap lacks Entry API** — No `entry()`, `or_insert()`, `or_insert_with()`.
6. **String lacks character/line iterators** — No `chars()`, `lines()`, `bytes()`.
7. **LSP only handles `didOpen`** — No `didChange`, so edits don't re-trigger diagnostics.

### Corrected audit findings (vs previous claims)

Items previously believed missing that are actually implemented:
- **Escape analysis IS wired** — `EscapeAnalysisPass` runs at Driver.cpp:998, sets `escape_status` attribute on `own.alloc` ops, and `OwnershipLowering.cpp:107` reads it to auto-promote to heap. No work needed.
- **DWARF debug info IS emitted** — `CodeGen.cpp:302` uses `DIBuilder` to create compile units, subprograms, and instruction-level debug locations. `--debug` works.
- **Option<T> is complete** — Has 12 methods including `zip`, `flatten`, `ok_or`, `filter`, `map`, `and_then`, `or_else`.

## Phase 1: Correctness Fixes (~80 LOC compiler)

### 1a. Propagate dead flags to CodeGen

**Problem:** `DriverOptions.maxThreads` and `DriverOptions.noPanicUnwind` are parsed but `runCodeGen()` at Driver.cpp:1092-1099 never copies them to `CodeGenOptions`.

**Files:**
- `include/asc/CodeGen/CodeGen.h` — Add to `CodeGenOptions` struct (after line 49):
  ```cpp
  unsigned maxThreads = 4;
  bool noPanicUnwind = false;
  ```
- `lib/Driver/Driver.cpp` — Add to `runCodeGen()` (after line 1099):
  ```cpp
  cgOpts.maxThreads = opts.maxThreads;
  cgOpts.noPanicUnwind = opts.noPanicUnwind;
  ```

**Downstream wiring:**
- `lib/Analysis/PanicScopeWrap.cpp` — When `noPanicUnwind` is true, skip `own.try_scope`/`own.catch_scope` emission entirely. The panic path becomes a trap (`__builtin_trap()`) instead of setjmp/longjmp unwind. The flag is passed via a module-level MLIR attribute: set `module->setAttr("asc.no_panic_unwind", BoolAttr::get(ctx, true))` in `lowerToHIR()` (in `Driver::lowerToHIR()`, after the MLIR module is created but before analysis passes run). PanicScopeWrap reads this attribute at the start of `runOnOperation()` and early-returns if set.
- `lib/Runtime/runtime.c` — The arena size `ASC_DEFAULT_ARENA_SIZE` is currently a compile-time constant (1 MB). To use `maxThreads`: declare a weak global `unsigned __asc_max_threads = 4;` in runtime.c. The compiler emits a strong definition `@__asc_max_threads = global i32 N` in LLVM IR (via HIRBuilder or CodeGen) to override it. The arena init reads `__asc_max_threads * PER_THREAD_STACK_SIZE` at startup. This avoids any dynamic linking — both symbols resolve at static link time.

### 1b. Fix operator trait signatures in Builtins.cpp

**Problem:** Add/Sub/Mul/Div traits are registered with `ref<Self>` receiver but `std/core/ops.ts` uses `own<Self>`. The mismatch means Sema trait checking validates against the wrong signature.

**Fix:** In `lib/Sema/Builtins.cpp`, change the 4 operator trait registrations from:
```
fn add(ref<Self>, ref<Self>): own<Self>
fn sub(ref<Self>, ref<Self>): own<Self>
fn mul(ref<Self>, ref<Self>): own<Self>
fn div(ref<Self>, ref<Self>): own<Self>
```
To:
```
fn add(own<Self>, own<Self>): own<Self>
fn sub(own<Self>, own<Self>): own<Self>
fn mul(own<Self>, own<Self>): own<Self>
fn div(own<Self>, own<Self>): own<Self>
```

This aligns with `std/core/ops.ts` and RFC-0011 (Rust's `Add` trait takes `self` by value, not by reference).

**Risk:** Any existing tests that call operator traits through Sema validation may need adjustment. Run full test suite after change. The actual codegen path for binary operators (`+`, `-`, `*`, `/`) goes through `SemaExpr.cpp` arithmetic handling, not through trait dispatch, so existing arithmetic tests should be unaffected.

### 1c. Add Sized marker trait

**Problem:** RFC-0011 specifies `Sized` as a marker trait. It's not registered in Builtins.cpp.

**Fix:** Add to `lib/Sema/Builtins.cpp` following the same pattern as `Send`/`Sync` (zero-method marker):
```cpp
auto *sizedTrait = ctx.create<TraitDecl>(
    "Sized", std::vector<GenericParam>{}, SourceLocation());
traitDecls["Sized"] = sizedTrait;
```

~10 lines. No enforcement needed yet — `?Sized` bounds are a future item.

### 1d. Validation

- Rebuild compiler: `cmake --build build/ -j$(sysctl -n hw.ncpu)`
- Run: `lit test/ --no-progress-bar` — all 245 must pass
- Verify: `./build/tools/asc/asc build --help` shows `--max-threads` and `--no-panic-unwind` in usage

---

## Phase 2: Iterator Adapter Methods on Trait (~150 LOC std)

### 2a. Add adapter methods to Iterator trait (`std/core/iter.ts`)

The adapter structs already exist as standalone types. Add 8 provided methods to the `Iterator` trait body that construct and return them:

```typescript
fn map<B>(own<Self>, f: (own<Item>) -> own<B>): own<Map<Self, B>> {
  return Map { iter: self, f: f };
}

fn filter(own<Self>, predicate: (ref<Item>) -> bool): own<Filter<Self>> {
  return Filter { iter: self, predicate: predicate };
}

fn take(own<Self>, n: usize): own<Take<Self>> {
  return Take { iter: self, remaining: n };
}

fn skip(own<Self>, n: usize): own<Skip<Self>> {
  return Skip { iter: self, remaining: n };
}

fn chain<U: Iterator>(own<Self>, other: own<U>): own<Chain<Self, U>> {
  return Chain { first: self, second: other, first_done: false };
}

fn zip<U: Iterator>(own<Self>, other: own<U>): own<Zip<Self, U>> {
  return Zip { a: self, b: other };
}

fn enumerate(own<Self>): own<Enumerate<Self>> {
  return Enumerate { iter: self, count: 0 };
}

fn flat_map<B, F>(own<Self>, f: F): own<FlatMap<Self, F, B>>
  where F: Fn(own<Item>) -> own<B>, B: Iterator
{
  return FlatMap { iter: self, f: f, inner: Option::None };
}
```

### 2b. Add FlatMap adapter struct

```typescript
struct FlatMap<I: Iterator, F, B: Iterator> {
  iter: own<I>,
  f: F,
  inner: Option<own<B>>,
}

impl<I: Iterator, F, B: Iterator> Iterator for FlatMap<I, F, B>
  where F: Fn(own<I::Item>) -> own<B>
{
  type Item = B::Item;

  fn next(refmut<Self>): Option<own<Item>> {
    loop {
      // Try to get next item from inner iterator.
      match self.inner {
        Option::Some(ref mut inner) => {
          match inner.next() {
            Option::Some(v) => { return Option::Some(v); },
            Option::None => { self.inner = Option::None; },
          }
        },
        Option::None => {},
      }
      // Get next outer item and map to inner iterator.
      match self.iter.next() {
        Option::Some(v) => { self.inner = Option::Some((self.f)(v)); },
        Option::None => { return Option::None; },
      }
    }
  }
}
```

### 2c. Add collect method

```typescript
// On Iterator trait:
fn collect<C: FromIterator<Item>>(own<Self>): own<C> {
  return C::from_iter(self);
}
```

This delegates to `FromIterator::from_iter()`. Requires `FromIterator` impls on target collections (Vec already has infrastructure for this via `extend`).

### 2d. Add FromIterator impl for Vec

```typescript
impl<T> FromIterator<T> for Vec<T> {
  fn from_iter<I: Iterator<Item = T>>(iter: own<I>): own<Vec<T>> {
    let v = Vec::new();
    loop {
      match iter.next() {
        Option::Some(item) => { v.push(item); },
        Option::None => { break; },
      }
    }
    return v;
  }
}
```

---

## Phase 3: Collections/String/IO Depth (~800 LOC std, ~40 LOC compiler)

### 3a. HashMap Entry API (`std/collections/hashmap.ts`, ~150 LOC)

```typescript
enum Entry<K, V> {
  Occupied(OccupiedEntry<K, V>),
  Vacant(VacantEntry<K, V>),
}

struct OccupiedEntry<K, V> {
  map: refmut<HashMap<K, V>>,
  index: usize,
}

struct VacantEntry<K, V> {
  map: refmut<HashMap<K, V>>,
  key: own<K>,
  hash: u64,
}
```

Methods on `Entry<K,V>`:
- `or_insert(default: own<V>): refmut<V>` — insert default if vacant, return ref to value
- `or_insert_with(f: () -> own<V>): refmut<V>` — lazy default
- `and_modify(f: (refmut<V>) -> void): Entry<K,V>` — mutate if occupied, return self

Methods on `OccupiedEntry`:
- `get(): ref<V>`, `get_mut(): refmut<V>`, `into_mut(): refmut<V>`
- `insert(value: own<V>): own<V>` — replace value, return old
- `remove(): own<V>` — remove entry, return value

Methods on `VacantEntry`:
- `insert(value: own<V>): refmut<V>` — insert and return ref to value
- `key(): ref<K>` — reference to the key

Method on `HashMap`:
- `entry(key: own<K>): Entry<K,V>` — probe for slot, return occupied or vacant entry
- `values_mut(): own<Vec<refmut<V>>>` — mutable value references

### 3b. String iterators (`std/string.ts`, ~100 LOC)

```typescript
struct Chars {
  ptr: *const u8,
  end: *const u8,
}

impl Iterator for Chars {
  type Item = char;
  fn next(refmut<Self>): Option<char> {
    // UTF-8 decode: read leading byte, determine sequence length (1-4),
    // decode codepoint, advance ptr, return as char.
  }
}

struct Lines {
  data: ref<str>,
  pos: usize,
}

impl Iterator for Lines {
  type Item = ref<str>;
  fn next(refmut<Self>): Option<ref<str>> {
    // Scan for \n or \r\n, return sub-slice, advance pos.
  }
}

struct Bytes {
  ptr: *const u8,
  end: *const u8,
}

impl Iterator for Bytes {
  type Item = u8;
  fn next(refmut<Self>): Option<u8> {
    // Simple pointer increment, return byte.
  }
}
```

Methods on `String`:
- `chars(): own<Chars>` — UTF-8 character iterator
- `lines(): own<Lines>` — line iterator
- `bytes(): own<Bytes>` — raw byte iterator
- `into_bytes(): own<Vec<u8>>` — consume string, return byte vector

### 3c. Vec additions (`std/vec.ts`, ~60 LOC)

- `truncate(len: usize): void` — drop elements from `len..self.len`, update length. If `len >= self.len`, no-op.
- `drain(start: usize, end: usize): own<Vec<T>>` — remove elements in range, return them as a new Vec, shift remaining left. Simplified version (returns Vec, not a draining iterator).

### 3d. BTreeMap completions (`std/collections/btreemap.ts`, ~80 LOC)

- `pop_first(): Option<(K, V)>` — remove and return the smallest key-value pair. Walk left to leftmost leaf, remove first entry, rebalance if needed.
- `pop_last(): Option<(K, V)>` — remove and return the largest key-value pair. Walk right to rightmost leaf, remove last entry, rebalance if needed.
- `range(from: ref<K>, to: ref<K>): own<Vec<(ref<K>, ref<V>)>>` — in-order traversal collecting pairs where `from <= key < to`. Returns Vec (not iterator) for simplicity.

### 3e. Atomic type additions (`std/sync/atomic.ts`, ~120 LOC)

Add `AtomicU64`, `AtomicUsize`, `AtomicPtr<T>` following the exact pattern of existing `AtomicI32`/`AtomicU32`/`AtomicI64`/`AtomicBool`:

Each atomic type gets: `new`, `load`, `store`, `swap`, `fetch_add`, `fetch_sub`, `fetch_and`, `fetch_or`, `fetch_xor`, `fetch_max`, `fetch_min`, `compare_exchange`, `compare_exchange_weak`.

`AtomicPtr<T>` gets: `new`, `load`, `store`, `swap`, `compare_exchange`, `compare_exchange_weak` (no arithmetic ops).

All backed by `@extern` wasm atomic intrinsics or `__atomic_*` builtins (same pattern as existing atomics in `lib/Runtime/atomics.c`).

### 3f. Channel improvements (`std/thread/channel.ts`, ~80 LOC)

- `recv_timeout(ms: u64): Result<own<T>, RecvError>` — spin-wait with deadline check. Uses `__asc_clock_monotonic()` from `wasi_clock.c`. Returns `RecvError::Timeout` if deadline exceeded, `RecvError::Disconnected` if sender dropped.
- `Clone` for `Sender<T>` — increment `Arc` refcount on underlying channel. Enables MPSC (multiple producer, single consumer) pattern.
- `iter(): RecvIter<T>` — blocking iterator that calls `recv()` in `next()`, returns `None` when channel disconnects.

```typescript
struct RecvIter<T> {
  rx: ref<Receiver<T>>,
}

impl<T> Iterator for RecvIter<T> {
  type Item = T;
  fn next(refmut<Self>): Option<own<T>> {
    match self.rx.recv() {
      Result::Ok(v) => Option::Some(v),
      Result::Err(_) => Option::None,
    }
  }
}
```

### 3g. LSP `didChange` handler (`lib/Driver/Driver.cpp`, ~40 LOC)

Add a `textDocument/didChange` handler in `runLsp()` that:
1. Extracts the document URI and content changes from the notification
2. Updates the in-memory source buffer with the new content
3. Re-runs Lex → Parse → Sema (same as `didOpen`)
4. Publishes diagnostics via `textDocument/publishDiagnostics`

This is the same logic as the existing `didOpen` handler, just triggered on content changes. The LSP currently only reacts to file opens — this means editing in an IDE produces stale diagnostics until the file is closed and reopened.

---

## Phase 4: Validation + Cleanup

### 4a. Compiler rebuild and test

- Rebuild: `cmake --build build/ -j$(sysctl -n hw.ncpu)`
- Run: `lit test/ --no-progress-bar` — all 245+ tests must pass

### 4b. New tests

| Test file | Covers |
|-----------|--------|
| `test/std/test_iter_adapters.ts` | map, filter, take, skip, chain, zip, enumerate, flat_map, collect |
| `test/std/test_hashmap_entry.ts` | entry(), or_insert, or_insert_with, and_modify, values_mut |
| `test/std/test_string_iterators.ts` | chars(), lines(), bytes(), into_bytes() |
| `test/std/test_atomic_u64.ts` | AtomicU64 load/store/fetch_add/compare_exchange |
| `test/std/test_channel_timeout.ts` | recv_timeout, Sender clone, RecvIter |

### 4c. Update CLAUDE.md

Update the RFC coverage table with audited numbers. Key corrections:
- Fix overall from ~84% to actual ~82% (post-push)
- Update individual RFC percentages
- Add AtomicU64/AtomicUsize/AtomicPtr to "What's Working" section
- Remove items from "Known Gaps" that are now addressed

---

## Build Sequence

```
Phase 1a,1b,1c (compiler) → rebuild → Phase 1d (validate)
    ↓
Phase 2a,2b,2c,2d (std/core/iter.ts) → validate
    ↓
Phase 3a-3f (std library, parallel — no compiler changes)
    ↓
Phase 3g (compiler — LSP) → rebuild → Phase 4 (full validate)
```

Two compiler rebuilds: after Phase 1 (Builtins.cpp + CodeGen.h + Driver.cpp) and after Phase 3g (Driver.cpp LSP handler).

## Files Modified

**Compiler (C++):**
- `include/asc/CodeGen/CodeGen.h` — add maxThreads, noPanicUnwind to CodeGenOptions
- `lib/Driver/Driver.cpp` — propagate flags in runCodeGen(), add didChange LSP handler
- `lib/Sema/Builtins.cpp` — fix operator trait signatures, add Sized trait
- `lib/Analysis/PanicScopeWrap.cpp` — read asc.no_panic_unwind module attribute
- `lib/Runtime/runtime.c` — read __asc_max_threads for arena sizing

**Standard Library (TypeScript):**
- `std/core/iter.ts` — 8 adapter methods + FlatMap struct + collect + FromIterator impl for Vec
- `std/collections/hashmap.ts` — Entry API (Entry/OccupiedEntry/VacantEntry), values_mut
- `std/string.ts` — Chars/Lines/Bytes iterators, chars/lines/bytes/into_bytes methods
- `std/vec.ts` — truncate, drain
- `std/collections/btreemap.ts` — pop_first, pop_last, range
- `std/sync/atomic.ts` — AtomicU64, AtomicUsize, AtomicPtr
- `std/thread/channel.ts` — recv_timeout, Clone on Sender, RecvIter

**Tests (new):**
- `test/std/test_iter_adapters.ts`
- `test/std/test_hashmap_entry.ts`
- `test/std/test_string_iterators.ts`
- `test/std/test_atomic_u64.ts`
- `test/std/test_channel_timeout.ts`

## Estimated LOC

| Phase | New/Modified LOC |
|-------|-----------------|
| Phase 1 (correctness fixes) | ~80 LOC compiler |
| Phase 2 (iterator adapters) | ~150 LOC std |
| Phase 3 (collections/string/IO) | ~630 LOC std + ~40 LOC compiler |
| Phase 4 (tests + CLAUDE.md) | ~300 LOC tests |
| **Total** | **~1,200 LOC** |

## Coverage Impact

| RFC | Before (audited) | After (projected) | Delta |
|-----|-----------------|-------------------|-------|
| 0004 Target Support | 82% | 86% | +4% |
| 0010 Toolchain/DX | 78% | 80% | +2% |
| 0011 Core Traits | 88% | 93% | +5% |
| 0013 Collections/String | 82% | 90% | +8% |
| 0014 Concurrency/IO | 78% | 86% | +8% |
| **Weighted Overall** | **~74%** | **~82%** | **+8%** |

## What This Does NOT Include

- `thread::scope` — requires compiler-level lifetime scoping for borrowed captures
- `select!` macro — requires macro expansion infrastructure in the compiler
- Wasm EH (throw/catch/rethrow) — deep codegen rewrite, separate initiative
- `derive(Serialize/Deserialize)` — requires macro expansion, blocks RFC-0016 Layer 1
- `impl Trait` return position — parser change with Sema implications
- `format!` macro — requires compiler macro support
- SHA-3 (Keccak sponge) — separate crypto initiative
- Lazy iterators for HashMap keys/values — returns Vecs instead
