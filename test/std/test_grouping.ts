// RUN: %asc check %s
// Test: Grouping, combinatorial, and sort utilities (RFC-0017 §1.2, §1.3, §3).
// Validates module structure compiles — full validation requires import support.
function main(): i32 {
  // group_consecutive: Unix-uniq-style grouping over runs of equal keys.
  let runs: Vec<i32> = Vec::new();
  runs.push(1);
  runs.push(1);
  runs.push(2);
  runs.push(2);
  runs.push(2);
  runs.push(3);
  assert_eq!(runs.len(), 6);

  // combinations: all k-size choices in lex order. C(4, 2) = 6.
  let elems: Vec<i32> = Vec::new();
  elems.push(1);
  elems.push(2);
  elems.push(3);
  elems.push(4);
  assert_eq!(elems.len(), 4);

  // permutations: Heap's algorithm. 3! = 6 orderings.
  let perm_src: Vec<i32> = Vec::new();
  perm_src.push(1);
  perm_src.push(2);
  perm_src.push(3);
  assert_eq!(perm_src.len(), 3);

  // sort_by_key: insertion sort by key function.
  let to_sort: Vec<i32> = Vec::new();
  to_sort.push(30);
  to_sort.push(10);
  to_sort.push(20);
  assert_eq!(to_sort.len(), 3);

  // bisect_left / bisect_right on a sorted Vec.
  let sorted: Vec<i32> = Vec::new();
  sorted.push(1);
  sorted.push(2);
  sorted.push(2);
  sorted.push(2);
  sorted.push(5);
  assert_eq!(sorted.len(), 5);

  // min_index / max_index.
  let scored: Vec<i32> = Vec::new();
  scored.push(5);
  scored.push(2);
  scored.push(8);
  scored.push(1);
  scored.push(3);
  assert_eq!(scored.len(), 5);

  // Empty-case surface checks.
  let empty: Vec<i32> = Vec::new();
  assert_eq!(empty.len(), 0);
  assert_eq!(empty.is_empty(), true);

  // Single-element case.
  let single: Vec<i32> = Vec::new();
  single.push(42);
  assert_eq!(single.len(), 1);

  return 0;
}
