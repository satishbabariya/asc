// std/collections/grouping.ts — Grouping, combinatorial, and sort utilities (RFC-0017 §1.2, §1.3, §3)

/// Group consecutive elements sharing the same key into sub-Vecs.
/// Runs of equal keys are grouped together (Unix `uniq`-style). O(n).
/// Key function borrows each element and returns an owned key used for equality.
function group_consecutive<T, K: Eq>(
  v: ref<Vec<T>>,
  key: (ref<T>) -> K
): own<Vec<Vec<T>>> {
  let result: Vec<Vec<T>> = Vec::new();
  if v.is_empty() { return result; }
  let current: Vec<T> = Vec::new();
  let prev_key: K = key(v.get(0).unwrap());
  current.push(*v.get(0).unwrap());
  let i: usize = 1;
  while i < v.len() {
    const item = v.get(i).unwrap();
    const k = key(item);
    if k.eq(&prev_key) {
      current.push(*item);
    } else {
      result.push(current);
      current = Vec::new();
      current.push(*item);
      prev_key = k;
    }
    i = i + 1;
  }
  result.push(current);
  return result;
}

/// Produce all k-combinations of `v` in lexicographic order (no repetition).
/// Each combination is a Vec<T> of copied elements. O(C(n,k) * k).
function combinations<T>(v: ref<Vec<T>>, k: usize): own<Vec<Vec<T>>> {
  let result: Vec<Vec<T>> = Vec::new();
  const n = v.len();
  if k == 0 {
    result.push(Vec::new());
    return result;
  }
  if k > n { return result; }
  // Index buffer starts at [0, 1, ..., k-1] and advances in lex order.
  let idx: Vec<usize> = Vec::new();
  let i: usize = 0;
  while i < k {
    idx.push(i);
    i = i + 1;
  }
  loop {
    let combo: Vec<T> = Vec::new();
    let j: usize = 0;
    while j < k {
      combo.push(*v.get(*idx.get(j).unwrap()).unwrap());
      j = j + 1;
    }
    result.push(combo);
    // Advance: find rightmost index below its max (n-k+pos), bump, then reset suffix.
    let pos: usize = k;
    let done = true;
    while pos > 0 {
      pos = pos - 1;
      if *idx.get(pos).unwrap() < n - k + pos {
        done = false;
        break;
      }
    }
    if done { break; }
    *idx.get_mut(pos).unwrap() = *idx.get(pos).unwrap() + 1;
    let m = pos + 1;
    while m < k {
      *idx.get_mut(m).unwrap() = *idx.get(m - 1).unwrap() + 1;
      m = m + 1;
    }
  }
  return result;
}

/// Produce all permutations of `v` via Heap's algorithm (non-recursive). O(n!).
/// Each permutation is a Vec<T> of copied elements.
function permutations<T>(v: ref<Vec<T>>): own<Vec<Vec<T>>> {
  let result: Vec<Vec<T>> = Vec::new();
  const n = v.len();
  if n == 0 {
    result.push(Vec::new());
    return result;
  }
  // Mutable working buffer and initial permutation.
  let a: Vec<T> = Vec::new();
  let first: Vec<T> = Vec::new();
  let i: usize = 0;
  while i < n {
    const elem = *v.get(i).unwrap();
    a.push(elem);
    first.push(elem);
    i = i + 1;
  }
  result.push(first);
  // Heap's algorithm: c[i] is the level-i loop counter.
  let c: Vec<usize> = Vec::new();
  i = 0;
  while i < n {
    c.push(0);
    i = i + 1;
  }
  i = 0;
  while i < n {
    if *c.get(i).unwrap() < i {
      // Even i swaps a[0] with a[i]; odd i swaps a[c[i]] with a[i].
      const swap_idx: usize = if i % 2 == 0 { 0 } else { *c.get(i).unwrap() };
      a.swap(swap_idx, i);
      let perm: Vec<T> = Vec::new();
      let j: usize = 0;
      while j < n {
        perm.push(*a.get(j).unwrap());
        j = j + 1;
      }
      result.push(perm);
      *c.get_mut(i).unwrap() = *c.get(i).unwrap() + 1;
      i = 0;
    } else {
      *c.get_mut(i).unwrap() = 0;
      i = i + 1;
    }
  }
  return result;
}

/// Sort `v` in place by a key function. Insertion sort (stable). O(n^2).
/// K must be Ord so keys can be compared.
function sort_by_key<T, K: Ord>(v: refmut<Vec<T>>, key: (ref<T>) -> K): void {
  if v.len() <= 1 { return; }
  let i: usize = 1;
  while i < v.len() {
    let j: usize = i;
    while j > 0 {
      const k_curr = key(v.get(j).unwrap());
      const k_prev = key(v.get(j - 1).unwrap());
      match k_curr.cmp(&k_prev) {
        Ordering::Less => {
          v.swap(j, j - 1);
          j = j - 1;
        },
        _ => { break; },
      }
    }
    i = i + 1;
  }
}

/// Binary search for leftmost insertion point for `target` in a sorted Vec. O(log n).
/// Returns the first index `i` such that `sorted[i] >= target` (or `len` if all less).
function bisect_left<T: Ord>(sorted: ref<Vec<T>>, target: ref<T>): usize {
  let lo: usize = 0;
  let hi: usize = sorted.len();
  while lo < hi {
    const mid = lo + (hi - lo) / 2;
    match sorted.get(mid).unwrap().cmp(target) {
      Ordering::Less => { lo = mid + 1; },
      _ => { hi = mid; },
    }
  }
  return lo;
}

/// Binary search for rightmost insertion point for `target` in a sorted Vec. O(log n).
/// Returns the first index `i` such that `sorted[i] > target` (or `len` if all <=).
function bisect_right<T: Ord>(sorted: ref<Vec<T>>, target: ref<T>): usize {
  let lo: usize = 0;
  let hi: usize = sorted.len();
  while lo < hi {
    const mid = lo + (hi - lo) / 2;
    match sorted.get(mid).unwrap().cmp(target) {
      Ordering::Greater => { hi = mid; },
      _ => { lo = mid + 1; },
    }
  }
  return lo;
}

/// Index of the minimum element. Returns None if `v` is empty.
function min_index<T: Ord>(v: ref<Vec<T>>): Option<usize> {
  if v.is_empty() { return Option::None; }
  let best: usize = 0;
  let i: usize = 1;
  while i < v.len() {
    match v.get(i).unwrap().cmp(v.get(best).unwrap()) {
      Ordering::Less => { best = i; },
      _ => {},
    }
    i = i + 1;
  }
  return Option::Some(best);
}

/// Index of the maximum element. Returns None if `v` is empty.
function max_index<T: Ord>(v: ref<Vec<T>>): Option<usize> {
  if v.is_empty() { return Option::None; }
  let best: usize = 0;
  let i: usize = 1;
  while i < v.len() {
    match v.get(i).unwrap().cmp(v.get(best).unwrap()) {
      Ordering::Greater => { best = i; },
      _ => {},
    }
    i = i + 1;
  }
  return Option::Some(best);
}
