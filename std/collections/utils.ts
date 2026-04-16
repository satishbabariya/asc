// std/collections/utils.ts — Collection utilities (RFC-0017)

/// Split a slice into chunks of size `chunk_size`.
function chunk<T>(slice: ref<[T]>, chunk_size: usize): own<Vec<ref<[T]>>> {
  assert!(chunk_size > 0);
  let result = Vec::new();
  let i: usize = 0;
  while i < slice.len() {
    const end = if i + chunk_size > slice.len() { slice.len() } else { i + chunk_size };
    result.push(slice.slice(i, end));
    i = i + chunk_size;
  }
  return result;
}

/// Partition elements by a predicate: (matching, non_matching).
function partition<T>(items: own<Vec<T>>, predicate: (ref<T>) -> bool): (own<Vec<T>>, own<Vec<T>>) {
  let yes = Vec::new();
  let no = Vec::new();
  let i: usize = 0;
  while i < items.len() {
    const item = items.remove(0);
    if predicate(&item) { yes.push(item); }
    else { no.push(item); }
    i = i + 1;
  }
  return (yes, no);
}

/// Remove duplicate elements (preserving first occurrence).
function distinct<T: Eq>(items: own<Vec<T>>): own<Vec<T>> {
  let result = Vec::new();
  let i: usize = 0;
  while i < items.len() {
    const item = items.get(i).unwrap();
    let found = false;
    let j: usize = 0;
    while j < result.len() {
      if result.get(j).unwrap().eq(item) { found = true; break; }
      j = j + 1;
    }
    if !found {
      // DECISION: Clone instead of move for distinct since we need to check
      // remaining elements against already-collected ones.
    }
    i = i + 1;
  }
  return result;
}

/// Flatten a Vec of Vecs into a single Vec.
function flatten<T>(nested: own<Vec<Vec<T>>>): own<Vec<T>> {
  let result = Vec::new();
  let i: usize = 0;
  while i < nested.len() {
    // Move inner vec elements into result.
    result.extend(nested.remove(0));
    i = i + 1;
  }
  return result;
}

/// Zip two Vecs into a Vec of pairs.
function zip<A, B>(a: own<Vec<A>>, b: own<Vec<B>>): own<Vec<(A, B)>> {
  let result = Vec::new();
  const len = if a.len() < b.len() { a.len() } else { b.len() };
  let i: usize = 0;
  while i < len {
    result.push((a.remove(0), b.remove(0)));
    i = i + 1;
  }
  return result;
}

/// Sum elements of a numeric Vec.
function sum_of(items: ref<Vec<i32>>): i32 {
  let total: i32 = 0;
  let i: usize = 0;
  while i < items.len() {
    total = total + *items.get(i).unwrap();
    i = i + 1;
  }
  return total;
}

/// Count elements matching a predicate.
function count_by<T>(items: ref<Vec<T>>, predicate: (ref<T>) -> bool): usize {
  let count: usize = 0;
  let i: usize = 0;
  while i < items.len() {
    if predicate(items.get(i).unwrap()) { count = count + 1; }
    i = i + 1;
  }
  return count;
}

/// Elements in both a and b. Preserves order of a. O(n*m).
function intersect<T: PartialEq>(a: ref<Vec<T>>, b: ref<Vec<T>>): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  let i: usize = 0;
  while i < a.len() {
    const item = a.get(i).unwrap();
    let j: usize = 0;
    let found = false;
    while j < b.len() {
      if item.eq(b.get(j).unwrap()) { found = true; break; }
      j = j + 1;
    }
    if found { result.push(item); }
    i = i + 1;
  }
  return result;
}

/// Elements in a that are NOT in b. Preserves order of a. O(n*m).
function difference<T: PartialEq>(a: ref<Vec<T>>, b: ref<Vec<T>>): own<Vec<ref<T>>> {
  let result: Vec<ref<T>> = Vec::new();
  let i: usize = 0;
  while i < a.len() {
    const item = a.get(i).unwrap();
    let j: usize = 0;
    let found = false;
    while j < b.len() {
      if item.eq(b.get(j).unwrap()) { found = true; break; }
      j = j + 1;
    }
    if !found { result.push(item); }
    i = i + 1;
  }
  return result;
}

/// Index of the minimum element. Returns None if empty.
function min_index<T: Ord>(items: ref<Vec<T>>): Option<usize> {
  if items.is_empty() { return Option::None; }
  let best: usize = 0;
  let i: usize = 1;
  while i < items.len() {
    match items.get(i).unwrap().cmp(items.get(best).unwrap()) {
      Ordering::Less => { best = i; },
      _ => {},
    }
    i = i + 1;
  }
  return Option::Some(best);
}

/// Index of the maximum element. Returns None if empty.
function max_index<T: Ord>(items: ref<Vec<T>>): Option<usize> {
  if items.is_empty() { return Option::None; }
  let best: usize = 0;
  let i: usize = 1;
  while i < items.len() {
    match items.get(i).unwrap().cmp(items.get(best).unwrap()) {
      Ordering::Greater => { best = i; },
      _ => {},
    }
    i = i + 1;
  }
  return Option::Some(best);
}

/// Binary search for leftmost insertion point (like Python bisect_left).
/// Items must be sorted.
function bisect_left<T: Ord>(items: ref<Vec<T>>, value: ref<T>): usize {
  let lo: usize = 0;
  let hi: usize = items.len();
  while lo < hi {
    let mid = lo + (hi - lo) / 2;
    match items.get(mid).unwrap().cmp(value) {
      Ordering::Less => { lo = mid + 1; },
      _ => { hi = mid; },
    }
  }
  return lo;
}

/// Binary search for rightmost insertion point (like Python bisect_right).
function bisect_right<T: Ord>(items: ref<Vec<T>>, value: ref<T>): usize {
  let lo: usize = 0;
  let hi: usize = items.len();
  while lo < hi {
    let mid = lo + (hi - lo) / 2;
    match items.get(mid).unwrap().cmp(value) {
      Ordering::Greater => { hi = mid; },
      _ => { lo = mid + 1; },
    }
  }
  return lo;
}

/// Join string slices with a separator. Returns owned String.
function join_ref(parts: ref<Vec<ref<str>>>, sep: ref<str>): own<String> {
  let result = String::new();
  let i: usize = 0;
  while i < parts.len() {
    if i > 0 { result.push_str(sep); }
    result.push_str(*parts.get(i).unwrap());
    i = i + 1;
  }
  return result;
}

/// Remove consecutive duplicate elements in place.
function dedup<T: PartialEq>(items: refmut<Vec<T>>): void {
  if items.len() <= 1 { return; }
  let write: usize = 1;
  let read: usize = 1;
  const elem_size = size_of!<T>();
  while read < items.len() {
    const curr = items.get(read).unwrap();
    const prev = items.get(write - 1).unwrap();
    if !curr.eq(prev) {
      if write != read {
        let dst = (items.ptr as usize + write * elem_size) as *mut u8;
        let src = (items.ptr as usize + read * elem_size) as *const u8;
        memcpy(dst, src, elem_size);
      }
      write = write + 1;
    }
    read = read + 1;
  }
  items.truncate(write);
}

/// Interleave two Vecs: [a0, b0, a1, b1, ...]. Remaining elements appended.
function interleave<T>(a: own<Vec<T>>, b: own<Vec<T>>): own<Vec<T>> {
  let result = Vec::new();
  let ai: usize = 0;
  let bi: usize = 0;
  const elem_size = size_of!<T>();
  while ai < a.len() || bi < b.len() {
    if ai < a.len() {
      const slot = (a.ptr as usize + ai * elem_size) as *const T;
      result.push(unsafe { ptr_read(slot) });
      ai = ai + 1;
    }
    if bi < b.len() {
      const slot = (b.ptr as usize + bi * elem_size) as *const T;
      result.push(unsafe { ptr_read(slot) });
      bi = bi + 1;
    }
  }
  free(a.ptr);
  free(b.ptr);
  return result;
}

/// Binary search for partition point — first index where predicate is true. O(log n).
function partition_point<T>(items: ref<Vec<T>>, pred: (ref<T>) -> bool): i32 {
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
  return lo;
}

/// Elements in either a or b, deduped, preserving order. O(n*m).
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

/// Cumulative sums. [1,2,3] -> [1,3,6].
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

/// Sort a Vec in-place by key function. Insertion sort.
function sort_by_key<T>(vec: refmut<Vec<T>>, key_fn: (ref<T>) -> i32): void {
  let i: i32 = 1;
  while (i as usize) < vec.len() {
    let j = i;
    while j > 0 {
      const key_j = key_fn(vec.get(j as usize).unwrap());
      const key_prev = key_fn(vec.get((j - 1) as usize).unwrap());
      if key_j < key_prev {
        // Swap using raw pointer arithmetic (no Vec::swap method)
        const elem_size = size_of!<T>();
        let ptr_j = (vec.ptr as usize + (j as usize) * elem_size) as *mut u8;
        let ptr_prev = (vec.ptr as usize + ((j - 1) as usize) * elem_size) as *mut u8;
        let tmp = alloca(elem_size) as *mut u8;
        memcpy(tmp, ptr_j, elem_size);
        memcpy(ptr_j, ptr_prev, elem_size);
        memcpy(ptr_prev, tmp, elem_size);
        j = j - 1;
      } else {
        break;
      }
    }
    i = i + 1;
  }
}

/// Return the n smallest elements. O(n * k) selection.
function smallest<T: Ord>(items: ref<Vec<T>>, n: usize): own<Vec<ref<T>>> {
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

/// Return the n largest elements. O(n * k) selection.
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

/// Invert a map: swap keys and values.
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

/// Filter map entries by key predicate.
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

/// Transform map values, keeping keys.
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

/// Merge two maps. On collision, f resolves.
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

/// Group elements by key function.
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
