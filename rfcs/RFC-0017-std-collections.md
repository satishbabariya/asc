# RFC-0017 — Std: Collections Utilities

| Field       | Value                              |
|-------------|------------------------------------|
| Status      | Accepted                           |
| Depends on  | RFC-0011, RFC-0013                 |
| Module path | `std::collections`                 |
| Inspired by | Deno `@std/collections`, Rust `itertools` |

## Summary

Provides pure functional utility functions over collections — things that belong above
the core `Vec`/`HashMap` iterator API but below an application framework. Every function
in this module is a free function (not a method), takes borrows wherever possible, and
returns owned values only when the operation genuinely produces new data. No hidden
allocations; each function's allocation behaviour is documented.

---

## 1. Array / Slice utilities

### Chunking and partitioning

```typescript
// Split a slice into fixed-size chunks. Last chunk may be smaller.
// Returns a borrowing iterator — zero allocation.
function chunk<T>(slice: ref<[T]>, size: usize): impl Iterator<Item=ref<[T]>>;

// Split into two Vecs based on a predicate. Allocates two Vecs.
function partition<T>(
  iter: own<impl IntoIterator<Item=own<T>>>,
  pred: ref<T> -> bool
): (own<Vec<T>>, own<Vec<T>>);  // (matching, non-matching)

// Split at first element satisfying predicate — no allocation
function partition_point<T>(slice: ref<[T]>, pred: ref<T> -> bool): usize;

// Divide into (head: ref<[T]>, tail: ref<[T]>) at index — no allocation
function split_at<T>(slice: ref<[T]>, i: usize): (ref<[T]>, ref<[T]>);
```

### Grouping

```typescript
// Group elements by key. Returns HashMap<K, Vec<T>>. Allocates.
// K must be Hash + Eq. key_fn borrows each element.
function group_by<T, K: Hash + Eq>(
  iter: own<impl IntoIterator<Item=own<T>>>,
  key_fn: ref<T> -> own<K>
): own<HashMap<K, Vec<T>>>;

// Group consecutive equal-key elements (like Unix uniq -c). No global HashMap.
// Returns iterator of (key, Vec<T>) groups in order of first appearance.
function group_consecutive<T, K: Eq>(
  iter: own<impl IntoIterator<Item=own<T>>>,
  key_fn: ref<T> -> own<K>
): impl Iterator<Item=(own<K>, own<Vec<T>>)>;
```

### Flattening and nesting

```typescript
// Flatten one level of nesting. Equivalent to iter.flat_map(|x| x).
// Available on Iterator as .flatten() already — this is the free-function form.
function flatten<T>(
  iter: own<impl IntoIterator<Item=own<impl IntoIterator<Item=own<T>>>>>
): impl Iterator<Item=own<T>>;

// Produce all k-combinations of elements from a slice (no repetition).
// Allocates each combination as a Vec<ref<T>>.
function combinations<T>(slice: ref<[T]>, k: usize)
  : impl Iterator<Item=own<Vec<ref<T>>>>;

// Produce all k-permutations.
function permutations<T>(slice: ref<[T]>, k: usize)
  : impl Iterator<Item=own<Vec<ref<T>>>>;
```

### Set-like operations (ordered, not hash-based)

```typescript
// Elements in a that are also in b. Preserves order of a. O(n*m).
// For large inputs, collect b into HashSet first.
function intersect<T: PartialEq>(a: ref<[T]>, b: ref<[T]>): own<Vec<ref<T>>>;

// Elements in a that are NOT in b. Preserves order of a. O(n*m).
function difference<T: PartialEq>(a: ref<[T]>, b: ref<[T]>): own<Vec<ref<T>>>;

// Elements in either a or b, deduped, preserving order of first appearance.
function union<T: PartialEq>(a: ref<[T]>, b: ref<[T]>): own<Vec<ref<T>>>;

// Remove consecutive duplicates (like Unix uniq). Mutates in place.
function dedup<T: PartialEq>(vec: refmut<Vec<T>>): void;

// Remove all duplicates, preserving first occurrence order. O(n log n).
// Requires T: Ord for the sort-based dedup.
function dedup_stable<T: Ord>(vec: refmut<Vec<T>>): void;

// Remove all duplicates by key. Preserves first occurrence. O(n) with hashing.
function dedup_by_key<T, K: Hash + Eq>(
  vec: refmut<Vec<T>>,
  key_fn: ref<T> -> own<K>
): void;
```

### Zipping and combining

```typescript
// Zip two iterables element-by-element, stopping at the shorter one.
// Same as Iterator::zip but as a free function.
function zip<A, B>(
  a: own<impl IntoIterator<Item=own<A>>>,
  b: own<impl IntoIterator<Item=own<B>>>
): impl Iterator<Item=(own<A>, own<B>)>;

// Zip with a combining function.
function zip_with<A, B, C>(
  a: own<impl IntoIterator<Item=own<A>>>,
  b: own<impl IntoIterator<Item=own<B>>>,
  f: (own<A>, own<B>) -> own<C>
): impl Iterator<Item=own<C>>;

// Unzip a sequence of pairs into two Vecs. Allocates two Vecs.
function unzip<A, B>(
  iter: own<impl IntoIterator<Item=(own<A>, own<B>)>>
): (own<Vec<A>>, own<Vec<B>>);

// Interleave two iterators, alternating elements until both are exhausted.
function interleave<T>(
  a: own<impl IntoIterator<Item=own<T>>>,
  b: own<impl IntoIterator<Item=own<T>>>
): impl Iterator<Item=own<T>>;
```

### Sampling and shuffling

```typescript
// Fisher-Yates shuffle in place. Requires a random source.
function shuffle<T>(slice: refmut<[T]>, rng: refmut<impl Rng>): void;

// Choose k distinct random elements from slice (without replacement).
// Returns own<Vec<ref<T>>> of k borrowed references.
function sample<T>(slice: ref<[T]>, k: usize, rng: refmut<impl Rng>)
  : own<Vec<ref<T>>>;

// Choose one random element. Returns None if slice is empty.
function choose<T>(slice: ref<[T]>, rng: refmut<impl Rng>): Option<ref<T>>;
```

---

## 2. HashMap / map utilities

```typescript
// Merge two maps. On key collision, f decides the winner.
// Allocates a new HashMap.
function merge_with<K: Hash + Eq, V>(
  a: own<HashMap<K, V>>,
  b: own<HashMap<K, V>>,
  f: (own<V>, own<V>) -> own<V>
): own<HashMap<K, V>>;

// Deep merge two nested HashMap<String, JsonValue> trees (recursive).
// For non-map values, b's value wins. Useful for config merging.
function deep_merge(
  base: own<HashMap<String, JsonValue>>,
  overlay: own<HashMap<String, JsonValue>>
): own<HashMap<String, JsonValue>>;

// Invert a map: swap keys and values. Requires V: Hash + Eq.
// If multiple keys map to the same value, last one wins.
function invert<K: Hash + Eq, V: Hash + Eq>(
  map: own<HashMap<K, V>>
): own<HashMap<V, K>>;

// Collect an iterator of (K, V) pairs into a HashMap, resolving duplicates with f.
function collect_with<K: Hash + Eq, V>(
  iter: own<impl IntoIterator<Item=(own<K>, own<V>)>>,
  f: (own<V>, own<V>) -> own<V>
): own<HashMap<K, V>>;

// Filter map entries by predicate on key. Returns new HashMap. Allocates.
function filter_keys<K: Hash + Eq + Clone, V>(
  map: ref<HashMap<K, V>>,
  pred: ref<K> -> bool
): own<HashMap<K, V>>;

// Map values, keeping keys unchanged.
function map_values<K: Hash + Eq + Clone, V, U>(
  map: own<HashMap<K, V>>,
  f: own<V> -> own<U>
): own<HashMap<K, U>>;

// Frequency count: how many times each value appears.
function frequencies<T: Hash + Eq>(
  iter: own<impl IntoIterator<Item=own<T>>>
): own<HashMap<T, usize>>;
```

---

## 3. Sorting and ordering utilities

```typescript
// Sort and return — fluent version of slice::sort_by_key
function sort_by_key<T, K: Ord>(
  vec: own<Vec<T>>,
  key_fn: ref<T> -> own<K>
): own<Vec<T>>;

// Stable sort preserving relative order of equal elements.
function stable_sort_by<T>(
  vec: own<Vec<T>>,
  cmp: (ref<T>, ref<T>) -> Ordering
): own<Vec<T>>;

// Return the n smallest elements (partial sort). O(n + k log k).
function smallest<T: Ord>(slice: ref<[T]>, n: usize): own<Vec<ref<T>>>;
function largest<T: Ord>(slice: ref<[T]>, n: usize): own<Vec<ref<T>>>;

// Index of minimum / maximum element.
function min_index<T: Ord>(slice: ref<[T]>): Option<usize>;
function max_index<T: Ord>(slice: ref<[T]>): Option<usize>;

// Binary search for insertion point (like Python's bisect).
function bisect_left<T: Ord>(slice: ref<[T]>, value: ref<T>): usize;
function bisect_right<T: Ord>(slice: ref<[T]>, value: ref<T>): usize;
```

---

## 4. String collection utilities

```typescript
// Join an iterator of strings with a separator. Allocates one String.
function join<S: AsRef<str>>(
  iter: own<impl IntoIterator<Item=own<S>>>,
  sep: ref<str>
): own<String>;

// Same but borrowing version — iter yields ref<str>.
function join_ref(
  iter: own<impl IntoIterator<Item=ref<str>>>,
  sep: ref<str>
): own<String>;

// Split string into Vec<String>. Allocates.
function split_collect(s: ref<str>, sep: ref<str>): own<Vec<String>>;

// Transpose a Vec<Vec<T>> (rows → columns). All rows must have equal length.
function transpose<T: Clone>(matrix: ref<Vec<Vec<T>>>): Result<own<Vec<Vec<T>>>, TransposeError>;
```

---

## 5. Iterator adapters (not already on `Iterator`)

These are free functions returning new iterator types, supplementing the `Iterator`
trait's provided methods.

```typescript
// Yield elements with running index: (0, elem), (1, elem), ...
// Same as Iterator::enumerate but as a free function.
function enumerate<T>(iter: own<impl IntoIterator<Item=own<T>>>)
  : impl Iterator<Item=(usize, own<T>)>;

// Yield (prev, curr) pairs from consecutive elements. First call yields (None, first).
function with_prev<T>(iter: own<impl IntoIterator<Item=own<T>>>)
  : impl Iterator<Item=(Option<own<T>>, own<T>)>;

// Cycle an iterator n times total (not infinitely). Clones on each cycle past first.
// T: Clone required for cycles > 1.
function cycle_n<T: Clone>(iter: own<impl IntoIterator<Item=own<T>>>, n: usize)
  : impl Iterator<Item=own<T>>;

// Produce cumulative sums. e.g. [1,2,3] → [1,3,6].
function scan_sum<T: Add<Output=T> + Copy>(iter: own<impl IntoIterator<Item=T>>)
  : impl Iterator<Item=T>;

// Tap: apply a side-effecting function to each element, then pass it through.
// Useful for debugging pipelines.
function tap<T>(
  iter: own<impl IntoIterator<Item=own<T>>>,
  f: ref<T> -> void
): impl Iterator<Item=own<T>>;
```

---

## 6. `Rng` trait (required by shuffle / sample)

```typescript
trait Rng {
  fn next_u32(refmut<Self>): u32;
  fn next_u64(refmut<Self>): u64;

  // Provided:
  fn next_f32(refmut<Self>): f32;   // [0.0, 1.0)
  fn next_f64(refmut<Self>): f64;   // [0.0, 1.0)
  fn next_range_u32(refmut<Self>, low: u32, high: u32): u32; // [low, high)
  fn fill_bytes(refmut<Self>, buf: refmut<[u8]>): void;
}

// Cryptographically secure RNG backed by Wasm's crypto.getRandomValues()
class SecureRng {
  static new(): own<SecureRng>;
  // implements Rng
}

// Deterministic seeded PRNG (xoshiro256** algorithm)
class SeededRng {
  static from_seed(seed: u64): own<SeededRng>;
  static from_entropy(): own<SeededRng>;  // seeds from SecureRng
  // implements Rng
}
```

---

## 7. Module layout

```
std::collections
├── chunk, partition, partition_point, split_at
├── group_by, group_consecutive
├── flatten, combinations, permutations
├── intersect, difference, union, dedup, dedup_stable, dedup_by_key
├── zip, zip_with, unzip, interleave
├── shuffle, sample, choose
├── merge_with, deep_merge, invert, collect_with, filter_keys, map_values, frequencies
├── sort_by_key, stable_sort_by, smallest, largest, min_index, max_index
├── bisect_left, bisect_right
├── join, join_ref, split_collect, transpose
├── enumerate, with_prev, cycle_n, scan_sum, tap
└── rng::{Rng, SecureRng, SeededRng}
```

Import pattern:

```typescript
import { group_by, chunk, frequencies } from 'std/collections';
import { SecureRng, shuffle } from 'std/collections';
```
