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
