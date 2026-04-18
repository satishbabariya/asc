# Correctness Fixes + RFC-0011/0013 Depth Push

| Field | Value |
|---|---|
| Date | 2026-04-15 |
| Status | ✅ Completed 2026-04-18 — all phases landed, 293/293 tests, RFC-0011 at 93%, RFC-0013 at 90% (targets exceeded) |
| Goal | Fix correctness issues, then push RFC-0011 and RFC-0013 to ~90% |
| Baseline | 237/237 tests passing, ~67% weighted RFC coverage |
| Target | RFC-0011: 80%→92%, RFC-0013: 65%→88%, overall: ~73% |

## Motivation

The RFC audit revealed two categories of problems:

1. **Correctness issues** — Sema trait registrations don't match RFC specs (Display/PartialOrd/Ord/Hash have wrong signatures), BTreeMap insert doesn't maintain sorted order, BTreeSet has stub methods, and 6 new std files sit uncommitted.

2. **Depth gaps in foundational RFCs** — RFC-0011 (Core Traits) and RFC-0013 (Collections/String) are the base layer everything else depends on. Pushing these to 90% has the highest leverage for future work.

## Phase 1: Correctness Fixes

### 1a. Fix Sema Trait Signatures (`lib/Sema/Builtins.cpp`)

The std library code already uses the correct signatures from the RFCs. Only the Sema registrations are wrong. No std code changes needed — only `Builtins.cpp`.

| Trait | Current Signature | Correct Signature (per RFC-0011) |
|-------|------------------|----------------------------------|
| Display | `fn display(ref<Self>): String` | `fn fmt(ref<Self>, refmut<Formatter>): Result<void, FmtError>` |
| PartialOrd | `fn partial_cmp(ref<Self>, ref<Self>): i32` | `fn partial_cmp(ref<Self>, ref<Self>): Option<Ordering>` |
| Ord | `fn cmp(ref<Self>, ref<Self>): i32` | `fn cmp(ref<Self>, ref<Self>): Ordering` |
| Hash | `fn hash(ref<Self>): u64` | `fn hash(ref<Self>, refmut<Hasher>): void` |

**Verified safe:** No tests reference the old `display` method name. No HIRBuilder or codegen code dispatches on these trait method names. The std code (`std/core/cmp.ts`, `std/collections/hashmap.ts`, `std/collections/heap.ts`, `std/collections/btreemap.ts`) already calls `.cmp()` expecting `Ordering`, `.hash(&hasher)` expecting a Hasher argument. The change aligns Sema with what the std code already does.

### 1b. Fix BTreeMap (`std/collections/btreemap.ts`)

Current state: `insert()` pushes to a single leaf without maintaining sorted order. No `remove()`. The `search_node()` function works correctly for lookup but insert doesn't match.

**Changes:**
- **`insert()`** — binary-search within node keys to find correct position, shift keys/values right, insert at position. When a node exceeds `MAX_KEYS` (11), split: median key promotes to parent, left/right children get half each. If root splits, create new root.
- **`remove()`** — search for key, remove from leaf (shift left). For internal nodes, replace with in-order predecessor then delete from leaf. If node underflows below `B-1` keys, rebalance: try borrowing from sibling, else merge with sibling.
- **`first_key_value()`** — walk left children to leftmost leaf, return first key/value.
- **`last_key_value()`** — walk right children to rightmost leaf, return last key/value.

**B-tree order:** Keep `B=6` (max 11 keys/node, min 5 keys/non-root node). This matches the existing constant.

### 1c. Fix BTreeSet (`std/collections/btreeset.ts`)

Once BTreeMap has `remove()`:
- **`remove()`** — delegate to `self.map.remove(value)`, return whether it was present.
- **`get()`** — delegate to `self.map.get(value)` and return the key reference.
- **`to_vec()`** — in-order traversal of the BTreeMap, collecting key references.

### 1d. Commit Untracked Files

Files ready to commit (703 LOC total):
- `std/async/throttle.ts` (85 lines)
- `std/collections/heap.ts` (128 lines)
- `std/collections/linked_list.ts` (119 lines)
- `std/crypto/sha512.ts` (229 lines)
- `std/encoding/percent.ts` (89 lines)
- `std/sync/lazy.ts` (53 lines)
- `test/std/test_percent.ts`
- `test/std/test_sha512.ts`

Plus modified files:
- `std/collections/btreeset.ts` (modifications)
- `std/path/posix.ts` (modifications)
- `.gitignore` (modifications)

### 1e. Validation

- Run `lit test/ --no-progress-bar` — all 237 tests must still pass.
- Rebuild compiler after Builtins.cpp change: `cmake --build build/ -j$(sysctl -n hw.ncpu)`.

---

## Phase 2: RFC-0011 Core Traits → ~92%

### 2a. Register Missing Traits in Sema (`lib/Sema/Builtins.cpp`)

Add registrations for traits that exist in std but aren't known to the compiler:

| Trait | Signature | Notes |
|-------|-----------|-------|
| Deref | `fn deref(ref<Self>): ref<Target>` | Associated type `Target` |
| DerefMut | `fn deref_mut(refmut<Self>): refmut<Target>` | Extends Deref |
| IntoIterator | `fn into_iter(own<Self>): own<IntoIter>` | Associated types `Item`, `IntoIter` |
| FromIterator\<T\> | `fn collect(iter: own<I>): own<Self>` | Generic `I: Iterator<Item=own<T>>` |
| IndexMut\<Idx\> | `fn index_mut(refmut<Self>, Idx): refmut<Output>` | Associated type `Output` |

These follow the exact same pattern as existing registrations (Drop, Clone, Index, Iterator). Each is ~25 lines of boilerplate in `Builtins.cpp`.

### 2b. Add Missing Iterator Combinators (`std/core/iter.ts`)

Currently has: count, last, nth, any, all, find, position, fold, for_each, sum, product + Map, Filter, Take, Skip, Chain, Zip, Enumerate adapters.

**Add:**
- `flat_map<B>(f)` — yields items from inner iterators produced by `f`
- `max()` / `min()` — fold-based, requires `T: Ord`
- `collect<C: FromIterator>()` — delegates to `C::collect(self)`
- `peekable()` — wraps in Peekable adapter with `peek()` method

**New adapter structs:**
- `FlatMap<I, F, B>` — stores outer iterator + current inner iterator + mapping function
- `Peekable<I>` — stores inner iterator + cached `Option<Item>`

### 2c. Fix Display Implementations (`std/core/fmt.ts`)

Current Display impls for i32/i64/f64/bool/str return the type name string (e.g., `"i32"`). Fix to return actual formatted values.

**Approach:** No formatting intrinsics exist in the compiler (`itoa`/`ftoa` not wired). Use manual digit extraction: divide-and-mod loop for integers (standard approach in self-hosted std libraries), write digits into a stack buffer, reverse, push to Formatter. For bool, write "true"/"false" literals. For f64, defer to a simplified decimal conversion (integer part + "." + fractional digits).

### 2d. Validation

- All 237 existing tests pass.
- Add test `test/std/test_iter_combinators.ts` — validates flat_map, max, min, peekable.

---

## Phase 3: RFC-0013 Collections/String → ~88%

### 3a. Vec Additions (`std/vec.ts`)

| Method | Signature | Implementation |
|--------|-----------|----------------|
| `sort()` | `fn sort(refmut<Self>): void` where `T: Ord` | In-place insertion sort for small vecs (≤16), otherwise quicksort partition + recurse |
| `sort_by(f)` | `fn sort_by(refmut<Self>, f: (ref<T>, ref<T>) -> Ordering): void` | Same algorithm with comparator |
| `retain(f)` | `fn retain(refmut<Self>, f: ref<T> -> bool): void` | Two-pointer compact: read pointer scans, write pointer fills, drop skipped elements |
| `truncate(len)` | `fn truncate(refmut<Self>, len: usize): void` | Drop elements from `len..self.len`, set `self.len = len` |
| `contains(v)` | `fn contains(ref<Self>, v: ref<T>): bool` where `T: PartialEq` | Linear scan with `eq()` |
| `iter()` | `fn iter(ref<Self>): VecIter<T>` | Returns borrowing iterator yielding `ref<T>` |
| `iter_mut()` | `fn iter_mut(refmut<Self>): VecIterMut<T>` | Returns mut iterator yielding `refmut<T>` |

**New structs:**
- `VecIter<T>` — `{ ptr: *const T, end: *const T }`, implements Iterator
- `VecIterMut<T>` — `{ ptr: *mut T, end: *mut T }`, implements Iterator

`drain()` deferred — requires owning iterator that mutates the source vec, which interacts with borrow checker in complex ways.

### 3b. String Additions (`std/string.ts`)

| Method | Signature | Implementation |
|--------|-----------|----------------|
| `find(s)` | `fn find(ref<Self>, s: ref<str>): Option<usize>` | Naive byte-by-byte search (sufficient for std) |
| `trim()` | `fn trim(ref<Self>): ref<str>` | Scan from both ends skipping ASCII whitespace, return sub-slice |
| `trim_start()` | `fn trim_start(ref<Self>): ref<str>` | Scan from start |
| `trim_end()` | `fn trim_end(ref<Self>): ref<str>` | Scan from end |
| `split(sep)` | `fn split(ref<Self>, sep: ref<str>): own<Vec<ref<str>>>` | Collect sub-slices between separator occurrences into a Vec |
| `replace(from, to)` | `fn replace(ref<Self>, from: ref<str>, to: ref<str>): own<String>` | Build new String, copying segments between matches |
| `as_bytes()` | `fn as_bytes(ref<Self>): ref<[u8]>` | Cast internal ptr+len as byte slice |
| `chars()` | `fn chars(ref<Self>): Chars` | Iterator that decodes UTF-8 codepoints |

**New structs:**
- `Chars` — `{ ptr: *const u8, end: *const u8 }`, implements Iterator\<char\>, decodes UTF-8 sequences

**Note:** `split()` returns `Vec<ref<str>>` rather than a lazy iterator for simplicity. The RFC specifies an iterator, but a Vec is correct for a first pass and still zero-allocation on the sub-slices themselves.

### 3c. HashMap Additions (`std/collections/hashmap.ts`)

| Method | Signature | Implementation |
|--------|-----------|----------------|
| `get_mut(k)` | `fn get_mut(refmut<Self>, k: ref<K>): Option<refmut<V>>` | Same probe as `get()`, return mutable ref |
| `keys()` | `fn keys(ref<Self>): own<Vec<ref<K>>>` | Scan occupied buckets, collect key refs |
| `values()` | `fn values(ref<Self>): own<Vec<ref<V>>>` | Scan occupied buckets, collect value refs |
| `entry(k)` | `fn entry(refmut<Self>, k: own<K>): Entry<K,V>` | Probe for slot, return Entry variant |

**Entry API structs:**
```
enum Entry<K, V> {
  Occupied(OccupiedEntry<K, V>),
  Vacant(VacantEntry<K, V>),
}
```
- `OccupiedEntry` — holds refmut to the bucket, provides `get()`, `get_mut()`, `insert()`, `remove()`
- `VacantEntry` — holds the key + refmut to the map, provides `insert(v)` which inserts and returns refmut to value
- `or_insert(default)` — on Entry, insert default if vacant
- `or_insert_with(f)` — lazy default
- `and_modify(f)` — mutate if occupied

**Note:** `keys()` and `values()` return collected Vecs rather than lazy iterators. Same rationale as String::split — correct first, optimize later.

### 3d. Validation

- All 237 existing tests pass.
- Add `test/std/test_btreemap.ts` — sorted insert, remove, first/last, duplicate key update.
- Add `test/std/test_vec_sort.ts` — sort correctness, retain, truncate, contains.
- Add `test/std/test_string_methods.ts` — trim, find, split, replace, chars.
- Add `test/std/test_hashmap_entry.ts` — entry API, get_mut, keys/values.

---

## Build Sequence

```
Phase 1a → rebuild compiler → Phase 1b,1c (parallel) → Phase 1d → Phase 1e (validate)
    ↓
Phase 2a → rebuild compiler → Phase 2b,2c (parallel) → Phase 2d (validate)
    ↓
Phase 3a,3b,3c (parallel, no compiler changes) → Phase 3d (validate)
```

Compiler rebuilds are needed after Phases 1a and 2a (Builtins.cpp changes). Phases 3a-3c are pure std library work — no compiler changes needed.

## Files Modified

**Compiler (C++):**
- `lib/Sema/Builtins.cpp` — fix 4 trait signatures + add 5 trait registrations

**Standard Library (TypeScript):**
- `std/collections/btreemap.ts` — real B-tree insert/remove/first/last
- `std/collections/btreeset.ts` — wire through to fixed BTreeMap
- `std/core/iter.ts` — flat_map, max, min, collect, peekable + adapter structs
- `std/core/fmt.ts` — fix Display impls for primitives
- `std/vec.ts` — sort, sort_by, retain, truncate, contains, iter, iter_mut + iterator structs
- `std/string.ts` — find, trim, trim_start, trim_end, split, replace, as_bytes, chars + Chars struct
- `std/collections/hashmap.ts` — get_mut, keys, values, entry + Entry/OccupiedEntry/VacantEntry

**Tests:**
- `test/std/test_btreemap.ts` (new)
- `test/std/test_vec_sort.ts` (new)
- `test/std/test_string_methods.ts` (new)
- `test/std/test_hashmap_entry.ts` (new)
- `test/std/test_iter_combinators.ts` (new)

## Estimated LOC

| Phase | New/Modified LOC |
|-------|-----------------|
| Phase 1 (correctness) | ~350 LOC (BTreeMap ~250, BTreeSet ~30, Builtins ~70) |
| Phase 2 (traits) | ~300 LOC (Builtins ~125, iter.ts ~120, fmt.ts ~55) |
| Phase 3 (collections) | ~500 LOC (Vec ~150, String ~130, HashMap ~220) |
| Tests | ~250 LOC |
| **Total** | **~1,400 LOC** |

## What This Does NOT Include

- `drain()` on Vec (complex borrow interaction)
- Lazy iterators for HashMap keys/values and String split (returns Vecs instead)
- `into_boxed_slice()` on Vec (requires Box<[T]> which needs unsized type support)
- `StaticArray<T, N>` type (requires const generics)
- `format!()` macro (requires macro expansion in compiler)
- Set operations on HashSet/BTreeSet (union, intersection, difference)
