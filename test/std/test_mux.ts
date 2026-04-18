// RUN: %asc check %s
// Test: MuxAsyncIterator<T> — register sources, round-robin fairly, drop
// exhausted sources, and return None when all are exhausted.

// Simple always-None source used to exercise the exhaustion path.
function empty_source(): Option<i32> {
  return Option::None;
}

// Always yields the same value (unbounded); used to prove that the mux
// polls sources round-robin without requiring mutation of outer state.
function one_source(): Option<i32> {
  return Option::Some(1);
}

function two_source(): Option<i32> {
  return Option::Some(2);
}

function three_source(): Option<i32> {
  return Option::Some(3);
}

function main(): i32 {
  // Empty mux — next() yields None immediately, source_count is 0.
  let m0: own<MuxAsyncIterator<i32>> = MuxAsyncIterator::new();
  assert!(m0.is_done());
  assert_eq!(m0.source_count(), 0);
  match m0.next() {
    Option::Some(_) => { assert!(false); },
    Option::None => {},
  }

  // Three sources: two always-yielding, one always-exhausted. After the
  // first sweep the exhausted source should be removed and the two live
  // sources should continue to interleave fairly.
  let m: own<MuxAsyncIterator<i32>> = MuxAsyncIterator::new();
  m.add(one_source);
  m.add(empty_source);
  m.add(two_source);
  assert_eq!(m.source_count(), 3);
  assert!(!m.is_done());

  // Pull a few rounds. Since one_source and two_source are unbounded,
  // we stop manually after N picks. Fair round-robin means we should
  // see both 1s and 2s in the output.
  let saw_one: bool = false;
  let saw_two: bool = false;
  let picks: i32 = 0;
  while picks < 6 {
    match m.next() {
      Option::Some(v) => {
        if v == 1 { saw_one = true; }
        if v == 2 { saw_two = true; }
        picks = picks + 1;
      },
      Option::None => { break; },
    }
  }
  assert!(saw_one);
  assert!(saw_two);
  // The exhausted empty_source should have been removed.
  assert_eq!(m.source_count(), 2);

  // Close drops remaining sources and refuses further values.
  m.close();
  assert!(m.is_done());
  assert_eq!(m.source_count(), 0);
  match m.next() {
    Option::Some(_) => { assert!(false); },
    Option::None => {},
  }

  // A second mux with all sources exhausted drains to None immediately.
  let m2: own<MuxAsyncIterator<i32>> = MuxAsyncIterator::new();
  m2.add(empty_source);
  m2.add(empty_source);
  m2.add(empty_source);
  assert_eq!(m2.source_count(), 3);
  match m2.next() {
    Option::Some(_) => { assert!(false); },
    Option::None => {},
  }
  // All three sources should have been removed during the failed sweep.
  assert_eq!(m2.source_count(), 0);
  assert!(m2.is_done());

  // Three always-yielding sources — verify that all three appear in the
  // output across a sweep, demonstrating round-robin fairness.
  let m3: own<MuxAsyncIterator<i32>> = MuxAsyncIterator::new();
  m3.add(one_source);
  m3.add(two_source);
  m3.add(three_source);
  let seen_one: bool = false;
  let seen_two: bool = false;
  let seen_three: bool = false;
  let k: i32 = 0;
  while k < 6 {
    match m3.next() {
      Option::Some(v) => {
        if v == 1 { seen_one = true; }
        if v == 2 { seen_two = true; }
        if v == 3 { seen_three = true; }
        k = k + 1;
      },
      Option::None => { break; },
    }
  }
  assert!(seen_one);
  assert!(seen_two);
  assert!(seen_three);
  m3.close();

  return 0;
}
