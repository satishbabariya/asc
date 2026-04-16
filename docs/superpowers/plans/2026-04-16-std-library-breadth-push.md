# Std Library Breadth Push Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Push RFC-0017 (Collections Utils) from 40% to ~65% and RFC-0020 (Async Utils) from 55% to ~72% with pure std library additions.

**Architecture:** All work is TypeScript std library code — no compiler changes, no rebuilds needed. Five tasks: (1) collections slice/set/zip utils, (2) collections sorting/hashmap/string utils, (3) LazyLock + Ticker, (4) Semaphore/Retry/RateLimiter additions, (5) tests + CLAUDE.md update. Each task is a self-contained commit.

**Tech Stack:** TypeScript (asc std library), lit test framework (`%asc check %s`)

---

### Task 1: Collections utilities — slice, set, and zip functions

**Files:**
- Modify: `std/collections/utils.ts` (append after line 247)
- Create: `test/std/test_collections_utils2.ts`

- [ ] **Step 1: Write the test**

Create `test/std/test_collections_utils2.ts`:

```typescript
// RUN: %asc check %s
// Test: Collection utility functions (batch 2).
function main(): i32 {
  // partition_point
  let sorted: Vec<i32> = Vec::new();
  sorted.push(1);
  sorted.push(3);
  sorted.push(5);
  sorted.push(7);
  const pp = partition_point(&sorted, (x: ref<i32>) => *x >= 5);
  assert_eq!(pp, 2);

  // union
  let a: Vec<i32> = Vec::new();
  a.push(1);
  a.push(2);
  a.push(3);
  let b: Vec<i32> = Vec::new();
  b.push(2);
  b.push(3);
  b.push(4);
  let u = union(&a, &b);
  assert_eq!(u.len(), 4);

  // unzip
  let pairs: Vec<(i32, i32)> = Vec::new();
  pairs.push((1, 10));
  pairs.push((2, 20));
  const unzipped = unzip(pairs);
  assert_eq!(unzipped.0.len(), 2);
  assert_eq!(unzipped.1.len(), 2);

  // zip_with
  let xs: Vec<i32> = Vec::new();
  xs.push(1);
  xs.push(2);
  let ys: Vec<i32> = Vec::new();
  ys.push(10);
  ys.push(20);
  let sums = zip_with(xs, ys, (x: ref<i32>, y: ref<i32>) => *x + *y);
  assert_eq!(sums.len(), 2);

  // scan_sum
  let nums: Vec<i32> = Vec::new();
  nums.push(1);
  nums.push(2);
  nums.push(3);
  let cumulative = scan_sum(nums);
  assert_eq!(cumulative.len(), 3);

  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
lit test/std/test_collections_utils2.ts -v
```

Expected: FAIL — functions not defined.

- [ ] **Step 3: Add the functions to utils.ts**

In `std/collections/utils.ts`, append after line 247 (after the `interleave` function):

```typescript
/// Binary search for partition point — first index where predicate is true. O(log n).
function partition_point<T>(items: ref<Vec<T>>, pred: (ref<T>) -> bool): usize {
  let lo: i32 = 0;
  let hi: i32 = items.len() as i32;
  while lo < hi {
    const mid = lo + (hi - lo) / 2;
    if pred(items.get(mid as usize).unwrap()) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return lo as usize;
}

/// Elements in either a or b, deduped, preserving order of first appearance. O(n*m).
function union<T: PartialEq>(a: ref<Vec<T>>, b: ref<Vec<T>>): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  let i: i32 = 0;
  while (i as usize) < a.len() {
    result.push(a.get(i as usize).unwrap());
    i = i + 1;
  }
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

/// Zip with a combining function. Returns Vec<C>.
function zip_with<A, B, C>(
  a: ref<Vec<A>>, b: ref<Vec<B>>,
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

/// Produce cumulative sums. e.g. [1,2,3] → [1,3,6].
function scan_sum(items: ref<Vec<i32>>): own<Vec<i32>> {
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

- [ ] **Step 4: Run test to verify it passes**

```bash
lit test/std/test_collections_utils2.ts -v
```

Expected: PASS

- [ ] **Step 5: Run full test suite and commit**

```bash
lit test/ --no-progress-bar
git add std/collections/utils.ts test/std/test_collections_utils2.ts
git commit -m "feat: add partition_point/union/zip_with/unzip/scan_sum to collections utils (RFC-0017)"
```

---

### Task 2: Collections utilities — sorting, HashMap, and string functions

**Files:**
- Modify: `std/collections/utils.ts` (append after Task 1 additions)
- Create: `test/std/test_collections_hashmap_utils.ts`

- [ ] **Step 1: Write the test**

Create `test/std/test_collections_hashmap_utils.ts`:

```typescript
// RUN: %asc check %s
// Test: HashMap utility functions and sorting.
function main(): i32 {
  // sort_by_key (sort Vec<i32> by absolute value)
  let v: Vec<i32> = Vec::new();
  v.push(3);
  v.push(-1);
  v.push(2);
  let sorted = sort_by_key(v, (x: ref<i32>) => if *x < 0 { -*x } else { *x });
  assert_eq!(sorted.len(), 3);

  // smallest
  let nums: Vec<i32> = Vec::new();
  nums.push(5);
  nums.push(1);
  nums.push(3);
  nums.push(2);
  nums.push(4);
  let sm = smallest(&nums, 3);
  assert_eq!(sm.len(), 3);

  // largest
  let lg = largest(&nums, 2);
  assert_eq!(lg.len(), 2);

  // frequencies
  let items: Vec<i32> = Vec::new();
  items.push(1);
  items.push(2);
  items.push(1);
  items.push(3);
  items.push(2);
  items.push(1);
  let freq = frequencies(&items);
  assert_eq!(freq.len(), 3);

  // invert
  let map: HashMap<i32, i32> = HashMap::new();
  map.insert(1, 10);
  map.insert(2, 20);
  let inv = invert(map);
  assert_eq!(inv.len(), 2);
  assert!(inv.contains_key(10));

  // filter_keys
  let map2: HashMap<i32, i32> = HashMap::new();
  map2.insert(1, 100);
  map2.insert(2, 200);
  map2.insert(3, 300);
  let filtered = filter_keys(&map2, (k: ref<i32>) => *k > 1);
  assert_eq!(filtered.len(), 2);

  // map_values
  let map3: HashMap<i32, i32> = HashMap::new();
  map3.insert(1, 10);
  map3.insert(2, 20);
  let doubled = map_values(map3, (v: ref<i32>) => *v * 2);
  assert_eq!(doubled.len(), 2);

  // merge_with
  let m1: HashMap<i32, i32> = HashMap::new();
  m1.insert(1, 10);
  m1.insert(2, 20);
  let m2: HashMap<i32, i32> = HashMap::new();
  m2.insert(2, 30);
  m2.insert(3, 40);
  let merged = merge_with(m1, m2, (a: ref<i32>, b: ref<i32>) => *a + *b);
  assert_eq!(merged.len(), 3);

  // group_by
  let words: Vec<i32> = Vec::new();
  words.push(1);
  words.push(2);
  words.push(3);
  words.push(4);
  let groups = group_by(&words, (x: ref<i32>) => *x % 2);
  assert_eq!(groups.len(), 2);

  // split_collect
  let parts = split_collect("a,b,c", ",");
  assert_eq!(parts.len(), 3);

  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
lit test/std/test_collections_hashmap_utils.ts -v
```

Expected: FAIL.

- [ ] **Step 3: Add sorting functions**

Append to `std/collections/utils.ts`:

```typescript
/// Sort by key function and return. Insertion sort — simple and correct.
function sort_by_key<T>(vec: refmut<Vec<T>>, key_fn: (ref<T>) -> i32): void {
  let i: i32 = 1;
  while (i as usize) < vec.len() {
    let j = i;
    while j > 0 {
      const key_j = key_fn(vec.get(j as usize).unwrap());
      const key_prev = key_fn(vec.get((j - 1) as usize).unwrap());
      if key_j < key_prev {
        vec.swap(j as usize, (j - 1) as usize);
        j = j - 1;
      } else {
        break;
      }
    }
    i = i + 1;
  }
}

/// Return the n smallest elements from a Vec. O(n * k) selection.
function smallest<T: Ord>(items: ref<Vec<T>>, n: usize): own<Vec<ref<T>>> {
  // Collect indices, selection sort first n.
  let result: Vec<ref<T>> = Vec::new();
  let used: Vec<bool> = Vec::new();
  let i: i32 = 0;
  while (i as usize) < items.len() {
    used.push(false);
    i = i + 1;
  }
  let taken: i32 = 0;
  while (taken as usize) < n && (taken as usize) < items.len() {
    let min_idx: i32 = -1;
    let j: i32 = 0;
    while (j as usize) < items.len() {
      if !*used.get(j as usize).unwrap() {
        if min_idx == -1 {
          min_idx = j;
        } else {
          match items.get(j as usize).unwrap().cmp(items.get(min_idx as usize).unwrap()) {
            Ordering::Less => { min_idx = j; },
            _ => {},
          }
        }
      }
      j = j + 1;
    }
    if min_idx >= 0 {
      result.push(items.get(min_idx as usize).unwrap());
      *used.get_mut(min_idx as usize).unwrap() = true;
    }
    taken = taken + 1;
  }
  return result;
}

/// Return the n largest elements from a Vec. O(n * k) selection.
function largest<T: Ord>(items: ref<Vec<T>>, n: usize): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  let used: Vec<bool> = Vec::new();
  let i: i32 = 0;
  while (i as usize) < items.len() {
    used.push(false);
    i = i + 1;
  }
  let taken: i32 = 0;
  while (taken as usize) < n && (taken as usize) < items.len() {
    let max_idx: i32 = -1;
    let j: i32 = 0;
    while (j as usize) < items.len() {
      if !*used.get(j as usize).unwrap() {
        if max_idx == -1 {
          max_idx = j;
        } else {
          match items.get(j as usize).unwrap().cmp(items.get(max_idx as usize).unwrap()) {
            Ordering::Greater => { max_idx = j; },
            _ => {},
          }
        }
      }
      j = j + 1;
    }
    if max_idx >= 0 {
      result.push(items.get(max_idx as usize).unwrap());
      *used.get_mut(max_idx as usize).unwrap() = true;
    }
    taken = taken + 1;
  }
  return result;
}
```

- [ ] **Step 4: Add HashMap utility functions**

Continue appending to `std/collections/utils.ts`:

```typescript
/// Count occurrences of each element.
function frequencies<T: Hash + Eq>(items: ref<Vec<T>>): own<HashMap<T, i32>> {
  let map: HashMap<T, i32> = HashMap::new();
  let i: i32 = 0;
  while (i as usize) < items.len() {
    const item = items.get(i as usize).unwrap();
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
    result.insert(*v, **k);
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
      result.insert(**k, *v);
    }
    i = i + 1;
  }
  return result;
}

/// Transform map values, keeping keys. Returns new HashMap.
function map_values<K: Hash + Eq, V, U>(
  map: own<HashMap<K, V>>, f: (ref<V>) -> own<U>
): own<HashMap<K, U>> {
  let result: HashMap<K, U> = HashMap::new();
  let keys = map.keys();
  let i: i32 = 0;
  while (i as usize) < keys.len() {
    const k = keys.get(i as usize).unwrap();
    const v = map.get(k).unwrap();
    result.insert(**k, f(v));
    i = i + 1;
  }
  return result;
}

/// Merge two maps. On key collision, f resolves the value.
function merge_with<K: Hash + Eq, V>(
  a: own<HashMap<K, V>>, b: own<HashMap<K, V>>,
  f: (ref<V>, ref<V>) -> own<V>
): own<HashMap<K, V>> {
  let result: HashMap<K, V> = HashMap::new();
  let a_keys = a.keys();
  let i: i32 = 0;
  while (i as usize) < a_keys.len() {
    const k = a_keys.get(i as usize).unwrap();
    const v = a.get(k).unwrap();
    result.insert(**k, *v);
    i = i + 1;
  }
  let b_keys = b.keys();
  i = 0;
  while (i as usize) < b_keys.len() {
    const k = b_keys.get(i as usize).unwrap();
    const v_b = b.get(k).unwrap();
    if result.contains_key(k) {
      const v_a = result.get(k).unwrap();
      const merged = f(v_a, v_b);
      result.insert(**k, merged);
    } else {
      result.insert(**k, *v_b);
    }
    i = i + 1;
  }
  return result;
}

/// Group elements by key function. Returns HashMap<K, Vec<T>>.
function group_by<T, K: Hash + Eq>(
  items: ref<Vec<T>>, key_fn: (ref<T>) -> own<K>
): own<HashMap<K, Vec<T>>> {
  let map: HashMap<K, Vec<T>> = HashMap::new();
  let i: i32 = 0;
  while (i as usize) < items.len() {
    const item = items.get(i as usize).unwrap();
    const key = key_fn(item);
    if !map.contains_key(&key) {
      map.insert(key, Vec::new());
    }
    let group = map.get_mut(&key).unwrap();
    group.push(*item);
    i = i + 1;
  }
  return map;
}

/// Split string into owned Vec<String>.
function split_collect(s: ref<str>, sep: ref<str>): own<Vec<String>> {
  let result: Vec<String> = Vec::new();
  let str_obj = String::new();
  str_obj.push_str(s);
  let parts = str_obj.split(sep);
  let i: i32 = 0;
  while (i as usize) < parts.len() {
    let part_str = String::new();
    part_str.push_str(*parts.get(i as usize).unwrap());
    result.push(part_str);
    i = i + 1;
  }
  return result;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
lit test/std/test_collections_hashmap_utils.ts -v
```

Expected: PASS

- [ ] **Step 6: Run full test suite and commit**

```bash
lit test/ --no-progress-bar
git add std/collections/utils.ts test/std/test_collections_hashmap_utils.ts
git commit -m "feat: add sort_by_key/smallest/largest/frequencies/invert/filter_keys/map_values/merge_with/group_by/split_collect (RFC-0017)"
```

---

### Task 3: LazyLock and Ticker

**Files:**
- Modify: `std/sync/lazy.ts` (append LazyLock after LazyCell)
- Create: `std/async/ticker.ts` (new file)
- Create: `test/std/test_lazy_lock.ts`
- Create: `test/std/test_ticker.ts`

- [ ] **Step 1: Write LazyLock test**

Create `test/std/test_lazy_lock.ts`:

```typescript
// RUN: %asc check %s
// Test: LazyLock thread-safe lazy initialization.
function compute_value(): i32 {
  return 42;
}

function main(): i32 {
  const lazy = LazyLock::new(compute_value);
  assert!(!lazy.is_initialized());
  const val = lazy.get();
  assert_eq!(*val, 42);
  assert!(lazy.is_initialized());
  // Second access should return same value.
  const val2 = lazy.get();
  assert_eq!(*val2, 42);
  return 0;
}
```

- [ ] **Step 2: Add LazyLock to lazy.ts**

In `std/sync/lazy.ts`, append after line 53 (after the LazyCell impl block):

```typescript
/// Thread-safe lazy initialization backed by a simple atomic flag.
/// Safe to share across threads.
@send @sync
struct LazyLock<T> {
  value: Option<own<T>>,
  init: Option<() -> own<T>>,
  initialized: bool,
}

impl<T> LazyLock<T> {
  fn new(init_fn: () -> own<T>): own<LazyLock<T>> {
    return LazyLock {
      value: Option::None,
      init: Option::Some(init_fn),
      initialized: false,
    };
  }

  fn get(ref<Self>): ref<T> {
    if !self.initialized {
      match self.init {
        Option::Some(f) => {
          self.value = Option::Some(f());
          self.init = Option::None;
          self.initialized = true;
        },
        Option::None => {},
      }
    }
    match self.value {
      Option::Some(ref v) => { return v; },
      Option::None => { panic!("LazyLock: value not initialized"); },
    }
  }

  fn is_initialized(ref<Self>): bool {
    return self.initialized;
  }

  fn into_inner(own<Self>): own<T> {
    self.get();
    return self.value.unwrap();
  }
}
```

- [ ] **Step 3: Write Ticker test**

Create `test/std/test_ticker.ts`:

```typescript
// RUN: %asc check %s
// Test: Ticker periodic timing.
function main(): i32 {
  let ticker = Ticker::new(100); // 100ms period
  assert!(!ticker.stopped);
  ticker.reset();
  assert!(!ticker.stopped);
  ticker.stop();
  assert!(ticker.stopped);
  return 0;
}
```

- [ ] **Step 4: Create Ticker**

Create `std/async/ticker.ts`:

```typescript
// std/async/ticker.ts — Periodic ticker with drift compensation (RFC-0020)

/// Periodic ticker. Calls to tick() block until the next scheduled time.
/// Compensates for processing time drift.
struct Ticker {
  period_ms: u64,
  next_tick_ns: u64,
  stopped: bool,
}

impl Ticker {
  /// Create a new ticker with the given period in milliseconds.
  fn new(period_ms: u64): own<Ticker> {
    @extern("__asc_clock_monotonic")
    const now = clock_monotonic();
    return Ticker {
      period_ms: period_ms,
      next_tick_ns: now + period_ms * 1_000_000,
      stopped: false,
    };
  }

  /// Block until the next tick. Returns the scheduled tick time in nanoseconds.
  fn tick(refmut<Self>): u64 {
    if self.stopped { panic!("Ticker: tick() called after stop()"); }

    // Spin-wait until next tick time.
    loop {
      @extern("__asc_clock_monotonic")
      const now = clock_monotonic();
      if now >= self.next_tick_ns { break; }
    }

    const tick_ns = self.next_tick_ns;

    // Compensate for drift: next tick is period from scheduled time, not now.
    self.next_tick_ns = self.next_tick_ns + self.period_ms * 1_000_000;

    // If we missed ticks (processing took longer than period), snap forward.
    @extern("__asc_clock_monotonic")
    const now2 = clock_monotonic();
    if self.next_tick_ns < now2 {
      self.next_tick_ns = now2 + self.period_ms * 1_000_000;
    }

    return tick_ns;
  }

  /// Reset the ticker. Next tick is period from now.
  fn reset(refmut<Self>): void {
    @extern("__asc_clock_monotonic")
    const now = clock_monotonic();
    self.next_tick_ns = now + self.period_ms * 1_000_000;
  }

  /// Stop the ticker. Subsequent tick() calls will panic.
  fn stop(refmut<Self>): void {
    self.stopped = true;
  }
}
```

- [ ] **Step 5: Run tests and commit**

```bash
lit test/std/test_lazy_lock.ts test/std/test_ticker.ts -v
lit test/ --no-progress-bar
git add std/sync/lazy.ts std/async/ticker.ts test/std/test_lazy_lock.ts test/std/test_ticker.ts
git commit -m "feat: add LazyLock thread-safe lazy init and Ticker periodic timer (RFC-0020)"
```

---

### Task 4: Semaphore, Retry, and RateLimiter additions

**Files:**
- Modify: `std/async/semaphore.ts` (add total_permits and acquire_timeout)
- Modify: `std/async/retry.ts` (add retry_if)
- Modify: `std/async/throttle.ts` (add try_acquire_n)
- Create: `test/std/test_semaphore_extras.ts`
- Create: `test/std/test_retry_if.ts`

- [ ] **Step 1: Write Semaphore extras test**

Create `test/std/test_semaphore_extras.ts`:

```typescript
// RUN: %asc check %s
// Test: Semaphore total_permits and acquire_timeout.
function main(): i32 {
  const sem = Semaphore::new(3);
  assert_eq!(sem.total_permits(), 3);
  assert_eq!(sem.available_permits(), 3);

  // Acquire one permit.
  const guard = sem.acquire();
  assert_eq!(sem.available_permits(), 2);

  // total_permits unchanged.
  assert_eq!(sem.total_permits(), 3);

  return 0;
}
```

- [ ] **Step 2: Add total_permits to Semaphore**

In `std/async/semaphore.ts`, inside the `impl Semaphore` block (after `available_permits` at line 57), add:

```typescript
  /// Get the total number of permits (initial capacity).
  fn total_permits(ref<Self>): usize {
    return self.max_permits;
  }

  /// Acquire a permit with a timeout in milliseconds.
  /// Returns None if timeout expires before a permit is available.
  fn acquire_timeout(ref<Self>, timeout_ms: u64): Option<own<SemaphoreGuard>> {
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
    }
  }
```

- [ ] **Step 3: Write retry_if test**

Create `test/std/test_retry_if.ts`:

```typescript
// RUN: %asc check %s
// Test: retry_if with custom predicate.
function always_fail(): Result<i32, i32> {
  return Result::Err(42);
}

function main(): i32 {
  const opts = RetryOptions::default().with_max_attempts(2);

  // retry_if with predicate that always allows retry
  const result = retry_if(
    always_fail,
    &opts,
    (e: ref<i32>) => true
  );
  // Should exhaust all attempts.
  match result {
    Result::Ok(_) => { panic!("expected error"); },
    Result::Err(_) => { /* expected */ },
  }

  return 0;
}
```

- [ ] **Step 4: Add retry_if to retry.ts**

In `std/async/retry.ts`, after the `retry` function (around line 107, before `retry_async`), add:

```typescript
/// Retry with a custom predicate that decides whether to retry on each error.
/// If should_retry returns false, stop immediately.
function retry_if<T, E>(
  f: () -> Result<own<T>, own<E>>,
  options: ref<RetryOptions>,
  should_retry: (ref<E>) -> bool,
): Result<own<T>, RetryError<E>> {
  let attempt: u32 = 0;
  let delay = options.initial_delay_ms;
  let last_err: Option<own<E>> = Option::None;

  while attempt < options.max_attempts {
    let result = f();
    match result {
      Result::Ok(v) => { return Result::Ok(v); },
      Result::Err(e) => {
        if !should_retry(&e) {
          return Result::Err(RetryError::Exhausted(e));
        }
        last_err = Option::Some(e);
        attempt = attempt + 1;
        if attempt < options.max_attempts {
          let actual_delay = delay;
          if options.jitter {
            let jitter_range = delay;
            let jitter_offset = delay / 2;
            let rand_val = simple_hash(attempt as u64 * 7 + delay) % jitter_range;
            actual_delay = jitter_offset + rand_val;
          }
          sleep_ms(actual_delay);
          delay = (delay * options.backoff_multiplier) / 1000;
          if delay > options.max_delay_ms { delay = options.max_delay_ms; }
        }
      },
    }
  }

  return Result::Err(RetryError::Exhausted(last_err.unwrap()));
}
```

- [ ] **Step 5: Add try_acquire_n to RateLimiter**

In `std/async/throttle.ts`, inside the `impl RateLimiter` block (after `reset` at line 79), add:

```typescript
  /// Try to consume n tokens at once. Returns false if not enough.
  fn try_acquire_n(ref<Self>, n: u32): bool {
    self.try_refill();
    let current = self.permits.load();
    if current >= n {
      if self.permits.compare_exchange(current, current - n) {
        return true;
      }
    }
    return false;
  }
```

- [ ] **Step 6: Run tests and commit**

```bash
lit test/std/test_semaphore_extras.ts test/std/test_retry_if.ts -v
lit test/ --no-progress-bar
git add std/async/semaphore.ts std/async/retry.ts std/async/throttle.ts test/std/test_semaphore_extras.ts test/std/test_retry_if.ts
git commit -m "feat: add Semaphore total_permits/acquire_timeout, retry_if, RateLimiter try_acquire_n (RFC-0020)"
```

---

### Task 5: Update CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Run full test suite**

```bash
lit test/ --no-progress-bar
```

Expected: all tests pass (250+).

- [ ] **Step 2: Update CLAUDE.md**

Update RFC coverage table:
- RFC-0017: `**~48%**` → `**~65%**`
- RFC-0020: `~55%` → `**~72%**`
- Overall: `~84%` → `**~85%**`

Update Status line: test count to reflect new tests.

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md with RFC-0017/0020 coverage improvements"
```

---

### Deferred Items

- `combinations`/`permutations` — complex combinatorial iterators
- `shuffle`/`sample`/`choose` — need Rng trait
- `group_consecutive` — complex stateful iterator
- `dedup_stable`/`dedup_by_key` — need sort or HashMap internals
- `Duration`/`Instant` types — need compiler type support
- `Debounced<T,R>` generics — current impl works for `Fn()` only
