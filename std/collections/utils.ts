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
