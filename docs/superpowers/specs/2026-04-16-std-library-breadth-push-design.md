# Std Library Breadth Push: RFC-0017 Collections Utils + RFC-0020 Async Utils

| Field | Value |
|---|---|
| Date | 2026-04-16 |
| Goal | Push RFC-0017 from 40% to ~65% and RFC-0020 from 55% to ~72% with pure std library additions |
| Baseline | 250/250 tests, ~84% overall weighted coverage |
| Target | RFC-0017 ~65%, RFC-0020 ~72%, overall ~85% |

## Motivation

RFC-0017 (Collections Utils) and RFC-0020 (Async Utils) are the two weakest std-library-only RFCs. Both require zero compiler changes — all work is TypeScript std library code. This makes them the safest, highest-volume push available.

The existing implementations provide good foundations:
- `std/collections/utils.ts` (247 LOC): chunk, partition, flatten, zip, intersect, difference, min_index, max_index, bisect_left, bisect_right, join_ref, dedup, interleave
- `std/async/` (886 LOC across 7 files): Semaphore, retry, deadline, Debounced, Throttled, RateLimiter, TaskPool, MuxChannel, LazyCell

## Phase 1: RFC-0017 — Simple Free Functions (~400 LOC)

All additions go in `std/collections/utils.ts`. These are pure functions with no new types needed.

### 1a. Slice utilities

```typescript
/// Split at first element satisfying predicate — binary search. O(log n).
function partition_point<T>(slice: ref<[T]>, pred: (ref<T>) -> bool): usize {
  let lo: i32 = 0;
  let hi: i32 = slice.len() as i32;
  while lo < hi {
    const mid = lo + (hi - lo) / 2;
    if pred(slice.get(mid as usize).unwrap()) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo as usize;
}

/// Split a slice at index i into (head, tail). No allocation.
function split_at<T>(slice: ref<[T]>, i: usize): (ref<[T]>, ref<[T]>) {
  assert!(i <= slice.len());
  return (slice.slice(0, i), slice.slice(i, slice.len()));
}
```

### 1b. Set-like operations

```typescript
/// Elements in either a or b, deduped, preserving order of first appearance. O(n*m).
function union<T: PartialEq>(a: ref<[T]>, b: ref<[T]>): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  // Add all from a.
  let i: i32 = 0;
  while (i as usize) < a.len() {
    result.push(a.get(i as usize).unwrap());
    i = i + 1;
  }
  // Add from b if not already in result (by checking against a).
  i = 0;
  while (i as usize) < b.len() {
    const elem = b.get(i as usize).unwrap();
    let found = false;
    let j: i32 = 0;
    while (j as usize) < a.len() {
      if a.get(j as usize).unwrap().eq(elem) { found = true; break; }
      j = j + 1;
    }
    if !found { result.push(elem); }
    i = i + 1;
  }
  return result;
}
```

### 1c. Zipping and combining

```typescript
/// Zip with a combining function. Returns Vec<C>.
function zip_with<A, B, C>(
  a: own<Vec<A>>, b: own<Vec<B>>,
  f: (ref<A>, ref<B>) -> own<C>
): own<Vec<C>> {
  let result: Vec<C> = Vec::new();
  let len = if a.len() < b.len() { a.len() } else { b.len() };
  let i: i32 = 0;
  while (i as usize) < len {
    result.push(f(a.get(i as usize).unwrap(), b.get(i as usize).unwrap()));
    i = i + 1;
  }
  return result;
}

/// Unzip a Vec of pairs into two Vecs.
function unzip<A, B>(pairs: own<Vec<(A, B)>>): (own<Vec<A>>, own<Vec<B>>) {
  let as_vec: Vec<A> = Vec::new();
  let bs_vec: Vec<B> = Vec::new();
  let i: i32 = 0;
  while (i as usize) < pairs.len() {
    const pair = pairs.get(i as usize).unwrap();
    as_vec.push(pair.0);
    bs_vec.push(pair.1);
    i = i + 1;
  }
  return (as_vec, bs_vec);
}
```

### 1d. Sorting utilities

```typescript
/// Sort by key function and return. Allocates a key vec for comparison.
function sort_by_key<T, K: Ord>(vec: own<Vec<T>>, key_fn: (ref<T>) -> own<K>): own<Vec<T>> {
  // Simple insertion sort by key. Sufficient for std library.
  let i: i32 = 1;
  while (i as usize) < vec.len() {
    let j = i;
    while j > 0 {
      const key_j = key_fn(vec.get(j as usize).unwrap());
      const key_prev = key_fn(vec.get((j - 1) as usize).unwrap());
      if key_j.cmp(&key_prev) == Ordering::Less {
        vec.swap(j as usize, (j - 1) as usize);
        j = j - 1;
      } else {
        break;
      }
    }
    i = i + 1;
  }
  return vec;
}

/// Return the n smallest elements from a slice. O(n * k).
function smallest<T: Ord>(slice: ref<[T]>, n: usize): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  // Collect all refs, sort, take n.
  let all: Vec<ref<T>> = Vec::new();
  let i: i32 = 0;
  while (i as usize) < slice.len() {
    all.push(slice.get(i as usize).unwrap());
    i = i + 1;
  }
  // Simple selection: iterate n times, find min each time.
  let taken: i32 = 0;
  while (taken as usize) < n && !all.is_empty() {
    let min_idx: i32 = 0;
    let j: i32 = 1;
    while (j as usize) < all.len() {
      if all.get(j as usize).unwrap().cmp(all.get(min_idx as usize).unwrap()) == Ordering::Less {
        min_idx = j;
      }
      j = j + 1;
    }
    result.push(*all.get(min_idx as usize).unwrap());
    all.remove(min_idx as usize);
    taken = taken + 1;
  }
  return result;
}

/// Return the n largest elements from a slice. O(n * k).
function largest<T: Ord>(slice: ref<[T]>, n: usize): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  let all: Vec<ref<T>> = Vec::new();
  let i: i32 = 0;
  while (i as usize) < slice.len() {
    all.push(slice.get(i as usize).unwrap());
    i = i + 1;
  }
  let taken: i32 = 0;
  while (taken as usize) < n && !all.is_empty() {
    let max_idx: i32 = 0;
    let j: i32 = 1;
    while (j as usize) < all.len() {
      if all.get(j as usize).unwrap().cmp(all.get(max_idx as usize).unwrap()) == Ordering::Greater {
        max_idx = j;
      }
      j = j + 1;
    }
    result.push(*all.get(max_idx as usize).unwrap());
    all.remove(max_idx as usize);
    taken = taken + 1;
  }
  return result;
}
```

### 1e. HashMap utilities

```typescript
/// Count occurrences of each element. Returns HashMap<T, i32>.
function frequencies<T: Hash + Eq>(items: own<Vec<T>>): own<HashMap<T, i32>> {
  let map: HashMap<T, i32> = HashMap::new();
  let i: i32 = 0;
  while (i as usize) < items.len() {
    const item = items.get(i as usize).unwrap();
    // Use contains_key + insert pattern since Entry API uses own<K>.
    if map.contains_key(item) {
      const count = *map.get(item).unwrap();
      map.insert(*item, count + 1);
    } else {
      map.insert(*item, 1);
    }
    i = i + 1;
  }
  return map;
}

/// Invert a map: swap keys and values. Last value wins on collision.
function invert<K: Hash + Eq, V: Hash + Eq>(map: own<HashMap<K, V>>): own<HashMap<V, K>> {
  let result: HashMap<V, K> = HashMap::new();
  let keys = map.keys();
  let i: i32 = 0;
  while (i as usize) < keys.len() {
    const k = keys.get(i as usize).unwrap();
    const v = map.get(k).unwrap();
    result.insert(*v, *k);
    i = i + 1;
  }
  return result;
}

/// Filter map entries by key predicate. Returns new HashMap.
function filter_keys<K: Hash + Eq, V>(
  map: ref<HashMap<K, V>>, pred: (ref<K>) -> bool
): own<HashMap<K, V>> {
  let result: HashMap<K, V> = HashMap::new();
  let keys = map.keys();
  let i: i32 = 0;
  while (i as usize) < keys.len() {
    const k = keys.get(i as usize).unwrap();
    if pred(k) {
      const v = map.get(k).unwrap();
      result.insert(*k, *v);
    }
    i = i + 1;
  }
  return result;
}

/// Transform values, keeping keys. Returns new HashMap.
function map_values<K: Hash + Eq, V, U>(
  map: own<HashMap<K, V>>, f: (ref<V>) -> own<U>
): own<HashMap<K, U>> {
  let result: HashMap<K, U> = HashMap::new();
  let keys = map.keys();
  let i: i32 = 0;
  while (i as usize) < keys.len() {
    const k = keys.get(i as usize).unwrap();
    const v = map.get(k).unwrap();
    result.insert(*k, f(v));
    i = i + 1;
  }
  return result;
}

/// Merge two maps. On key collision, f decides the value.
function merge_with<K: Hash + Eq, V>(
  a: own<HashMap<K, V>>, b: own<HashMap<K, V>>,
  f: (ref<V>, ref<V>) -> own<V>
): own<HashMap<K, V>> {
  let result: HashMap<K, V> = HashMap::new();
  // Add all from a.
  let a_keys = a.keys();
  let i: i32 = 0;
  while (i as usize) < a_keys.len() {
    const k = a_keys.get(i as usize).unwrap();
    const v = a.get(k).unwrap();
    result.insert(*k, *v);
    i = i + 1;
  }
  // Add from b, resolving collisions with f.
  let b_keys = b.keys();
  i = 0;
  while (i as usize) < b_keys.len() {
    const k = b_keys.get(i as usize).unwrap();
    const v_b = b.get(k).unwrap();
    if result.contains_key(k) {
      const v_a = result.get(k).unwrap();
      const merged = f(v_a, v_b);
      result.insert(*k, merged);
    } else {
      result.insert(*k, *v_b);
    }
    i = i + 1;
  }
  return result;
}
```

### 1f. String collection utilities

```typescript
/// Split string into Vec<String> (owned). Allocates.
function split_collect(s: ref<str>, sep: ref<str>): own<Vec<String>> {
  let result: Vec<String> = Vec::new();
  // Delegate to String::split which returns Vec<ref<str>>.
  const str_obj = String::new();
  str_obj.push_str(s);
  const parts = str_obj.split(sep);
  let i: i32 = 0;
  while (i as usize) < parts.len() {
    result.push(String::from(*parts.get(i as usize).unwrap()));
    i = i + 1;
  }
  return result;
}
```

### 1g. Iterator adapter: group_by

```typescript
/// Group elements by key. Returns HashMap<K, Vec<T>>.
function group_by<T, K: Hash + Eq>(
  items: own<Vec<T>>, key_fn: (ref<T>) -> own<K>
): own<HashMap<K, Vec<T>>> {
  let map: HashMap<K, Vec<T>> = HashMap::new();
  // Iterate and group.
  let i: i32 = 0;
  while (i as usize) < items.len() {
    const item = items.get(i as usize).unwrap();
    const key = key_fn(item);
    if !map.contains_key(&key) {
      map.insert(key, Vec::new());
    }
    // Get the group and push.
    // Note: this simplified version uses contains_key + get_mut pattern.
    let group = map.get_mut(&key).unwrap();
    group.push(*item);
    i = i + 1;
  }
  return map;
}

/// Produce cumulative sums. e.g. [1,2,3] → [1,3,6].
function scan_sum(items: own<Vec<i32>>): own<Vec<i32>> {
  let result: Vec<i32> = Vec::new();
  let acc: i32 = 0;
  let i: i32 = 0;
  while (i as usize) < items.len() {
    acc = acc + *items.get(i as usize).unwrap();
    result.push(acc);
    i = i + 1;
  }
  return result;
}
```

---

## Phase 2: RFC-0020 — Async Utility Additions (~300 LOC)

### 2a. LazyLock (thread-safe lazy init)

**File:** `std/sync/lazy.ts` (already has LazyCell at 53 LOC)

```typescript
/// Thread-safe lazy initialization backed by Once.
/// Unlike LazyCell, safe to share across threads via @send @sync.
@send @sync
struct LazyLock<T: Send + Sync> {
  once: own<Once>,
  value: Option<own<T>>,
  init: Option<() -> own<T>>,
}

impl<T: Send + Sync> LazyLock<T> {
  fn new(init: () -> own<T>): own<LazyLock<T>> {
    return LazyLock {
      once: Once::new(),
      value: Option::None,
      init: Option::Some(init),
    };
  }

  fn get(ref<Self>): ref<T> {
    self.once.call_once(() => {
      match self.init {
        Option::Some(f) => {
          self.value = Option::Some(f());
          self.init = Option::None;
        },
        Option::None => {},
      }
    });
    match self.value {
      Option::Some(ref v) => v,
      Option::None => { panic!("LazyLock: init did not set value"); },
    }
  }

  fn is_initialized(ref<Self>): bool {
    return self.once.is_completed();
  }

  fn into_inner(own<Self>): own<T> {
    self.get(); // Ensure initialized.
    return self.value.unwrap();
  }
}
```

### 2b. Ticker (periodic tick with drift compensation)

**File:** `std/async/ticker.ts` (new file)

```typescript
/// Periodic ticker with drift compensation.
/// Uses monotonic clock for timing.
struct Ticker {
  period_ms: u64,
  next_tick_ns: u64,
  stopped: bool,
}

impl Ticker {
  fn new(period_ms: u64): own<Ticker> {
    @extern("__asc_clock_monotonic")
    const now = clock_monotonic();
    return Ticker {
      period_ms: period_ms,
      next_tick_ns: now + period_ms * 1_000_000,
      stopped: false,
    };
  }

  /// Block until the next tick. Returns tick count.
  fn tick(refmut<Self>): u64 {
    if self.stopped { panic!("Ticker: tick() called after stop()"); }

    @extern("__asc_clock_monotonic")
    let now = clock_monotonic();

    // Spin-wait until next tick time.
    while now < self.next_tick_ns {
      // Yield CPU briefly.
      @extern("__asc_clock_monotonic")
      now = clock_monotonic();
    }

    // Compensate for drift: next tick is period from scheduled time, not now.
    const tick_ns = self.next_tick_ns;
    self.next_tick_ns = self.next_tick_ns + self.period_ms * 1_000_000;

    // If we're way behind (missed ticks), snap forward.
    if self.next_tick_ns < now {
      self.next_tick_ns = now + self.period_ms * 1_000_000;
    }

    return tick_ns;
  }

  /// Reset the ticker (next tick is period from now).
  fn reset(refmut<Self>): void {
    @extern("__asc_clock_monotonic")
    const now = clock_monotonic();
    self.next_tick_ns = now + self.period_ms * 1_000_000;
  }

  /// Stop the ticker.
  fn stop(refmut<Self>): void {
    self.stopped = true;
  }
}
```

### 2c. Semaphore additions

**File:** `std/async/semaphore.ts` (already has Semaphore at 92 LOC)

Add two methods to the existing `impl Semaphore` block:

```typescript
  /// Total number of permits (initial capacity).
  fn total_permits(ref<Self>): usize {
    return self.total;
  }

  /// Acquire a permit with a timeout in milliseconds.
  /// Returns None if timeout expires before a permit is available.
  fn acquire_timeout(ref<Self>, timeout_ms: u64): Option<SemaphoreGuard> {
    @extern("__asc_clock_monotonic")
    const start_ns = clock_monotonic();
    const deadline_ns = start_ns + timeout_ms * 1_000_000;

    loop {
      match self.try_acquire() {
        Option::Some(guard) => { return Option::Some(guard); },
        Option::None => {},
      }

      @extern("__asc_clock_monotonic")
      const now = clock_monotonic();
      if now >= deadline_ns {
        return Option::None;
      }
      // Brief spin.
    }
  }
```

This requires the Semaphore struct to have a `total` field. Check the current implementation — if it doesn't store total permits, add the field and set it in `new()`.

### 2d. Retry additions

**File:** `std/async/retry.ts` (already has retry at 161 LOC)

Add `retry_if` after the existing `retry` function:

```typescript
/// Retry with a custom should_retry predicate.
/// If should_retry returns false, stop immediately.
function retry_if<T, E>(
  opts: RetryOptions,
  f: () -> Result<own<T>, own<E>>,
  should_retry: (ref<E>) -> bool
): Result<own<T>, RetryError<E>> {
  let delay_ms = opts.initial_delay_ms;
  let attempts: i32 = 0;
  let last_err: Option<own<E>> = Option::None;

  while attempts < opts.max_attempts as i32 {
    match f() {
      Result::Ok(v) => { return Result::Ok(v); },
      Result::Err(e) => {
        if !should_retry(&e) {
          return Result::Err(RetryError::Failed(e));
        }
        last_err = Option::Some(e);
        attempts = attempts + 1;
        if attempts < opts.max_attempts as i32 {
          // Sleep for delay with jitter.
          const jitter = (delay_ms as i32 * opts.jitter_percent as i32) / 100;
          const actual_delay = delay_ms as i32 + jitter;
          @extern("__asc_sleep_ms")
          sleep_ms(actual_delay as u64);
          delay_ms = delay_ms * opts.multiplier as u64;
          if delay_ms > opts.max_delay_ms { delay_ms = opts.max_delay_ms; }
        }
      },
    }
  }

  match last_err {
    Option::Some(e) => { return Result::Err(RetryError::Exhausted(e)); },
    Option::None => { panic!("retry_if: unreachable"); },
  }
}
```

### 2e. RateLimiter addition

**File:** `std/async/throttle.ts` (already has RateLimiter)

Add `try_acquire_n` to the existing `impl RateLimiter` block:

```typescript
  /// Consume n tokens at once. Returns false if not enough tokens.
  fn try_acquire_n(ref<Self>, n: u32): bool {
    self.refill();
    if self.tokens >= n as f64 {
      self.tokens = self.tokens - n as f64;
      return true;
    }
    return false;
  }
```

---

## Phase 3: Tests (~200 LOC)

### Test files

- `test/std/test_collections_utils2.ts` — partition_point, split_at, union, zip_with, unzip, sort_by_key, smallest, largest, frequencies, invert, filter_keys, map_values, merge_with, group_by, scan_sum, split_collect
- `test/std/test_lazy_lock.ts` — LazyLock::new, get, is_initialized
- `test/std/test_ticker.ts` — Ticker::new, tick, reset, stop
- `test/std/test_retry_if.ts` — retry_if with custom predicate

---

## Build Sequence

No compiler rebuilds needed — all work is TypeScript std library.

```
Phase 1 (collections utils) → lit test/ → commit
Phase 2a (LazyLock) → lit test/ → commit
Phase 2b (Ticker) → lit test/ → commit
Phase 2c-2e (Semaphore/Retry/RateLimiter additions) → lit test/ → commit
Phase 3 (all tests) → lit test/ → commit
```

## Files Modified

**Standard Library (TypeScript):**
- `std/collections/utils.ts` — 15+ new free functions (~400 LOC)
- `std/sync/lazy.ts` — add LazyLock struct (~60 LOC)
- `std/async/ticker.ts` — new file, Ticker struct (~80 LOC)
- `std/async/semaphore.ts` — add total_permits, acquire_timeout (~30 LOC)
- `std/async/retry.ts` — add retry_if (~40 LOC)
- `std/async/throttle.ts` — add try_acquire_n (~10 LOC)

**Tests (new):**
- `test/std/test_collections_utils2.ts`
- `test/std/test_lazy_lock.ts`
- `test/std/test_ticker.ts`
- `test/std/test_retry_if.ts`

## Estimated LOC

| Phase | LOC |
|-------|-----|
| Phase 1: Collections utils | ~400 TS |
| Phase 2: Async utils | ~220 TS |
| Phase 3: Tests | ~200 TS |
| **Total** | **~820 LOC** |

## Coverage Impact

| RFC | Before | After | Change |
|-----|--------|-------|--------|
| 0017 Collections Utils | 40% | ~65% | +25% |
| 0020 Async Utilities | 55% | ~72% | +17% |
| **Overall** | **~84%** | **~85%** | **+1%** |

## What This Does NOT Include

- `combinations`/`permutations` — combinatorial iterators are complex and rarely used
- `shuffle`/`sample`/`choose` — need Rng trait which doesn't exist
- `group_consecutive` — complex iterator adapter with state machine
- `cycle_n` — needs Clone support in the iterator
- `transpose` — needs Clone + equal-length validation
- `deep_merge` — depends on JsonValue type
- `dedup_stable`/`dedup_by_key` — need sort or HashMap internals
- `Duration`/`Instant` types — need compiler type support; raw u64 ms works fine
- `Debounced<T,R>` with generic args — current Debounced works for Fn() only
