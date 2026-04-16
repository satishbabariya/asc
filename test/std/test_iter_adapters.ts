// RUN: %asc check %s
// Test: Iterator adapter methods on trait.
function main(): i32 {
  // map: double each element
  let v: Vec<i32> = Vec::new();
  v.push(1);
  v.push(2);
  v.push(3);
  let mapped: Vec<i32> = Vec::new();
  let iter = v.iter().map((x: ref<i32>) => *x * 2);
  loop {
    match iter.next() {
      Option::Some(val) => { mapped.push(val); },
      Option::None => { break; },
    }
  }
  assert_eq!(mapped.len(), 3);

  // filter: keep only even
  let v2: Vec<i32> = Vec::new();
  v2.push(1);
  v2.push(2);
  v2.push(3);
  v2.push(4);
  let filtered: Vec<i32> = Vec::new();
  let iter2 = v2.iter().filter((x: ref<i32>) => *x % 2 == 0);
  loop {
    match iter2.next() {
      Option::Some(val) => { filtered.push(*val); },
      Option::None => { break; },
    }
  }
  assert_eq!(filtered.len(), 2);

  // take
  let v3: Vec<i32> = Vec::new();
  v3.push(10);
  v3.push(20);
  v3.push(30);
  v3.push(40);
  let taken: Vec<i32> = Vec::new();
  let iter3 = v3.iter().take(2);
  loop {
    match iter3.next() {
      Option::Some(val) => { taken.push(*val); },
      Option::None => { break; },
    }
  }
  assert_eq!(taken.len(), 2);

  // skip
  let v4: Vec<i32> = Vec::new();
  v4.push(1);
  v4.push(2);
  v4.push(3);
  let skipped: Vec<i32> = Vec::new();
  let iter4 = v4.iter().skip(2);
  loop {
    match iter4.next() {
      Option::Some(val) => { skipped.push(*val); },
      Option::None => { break; },
    }
  }
  assert_eq!(skipped.len(), 1);

  // enumerate
  let v5: Vec<i32> = Vec::new();
  v5.push(100);
  v5.push(200);
  let count: i32 = 0;
  let iter5 = v5.iter().enumerate();
  loop {
    match iter5.next() {
      Option::Some(pair) => { count = count + 1; },
      Option::None => { break; },
    }
  }
  assert_eq!(count, 2);

  // chain
  let a: Vec<i32> = Vec::new();
  a.push(1);
  a.push(2);
  let b: Vec<i32> = Vec::new();
  b.push(3);
  b.push(4);
  let chained_count: i32 = 0;
  let iter6 = a.iter().chain(b.iter());
  loop {
    match iter6.next() {
      Option::Some(_) => { chained_count = chained_count + 1; },
      Option::None => { break; },
    }
  }
  assert_eq!(chained_count, 4);

  // zip
  let x: Vec<i32> = Vec::new();
  x.push(1);
  x.push(2);
  let y: Vec<i32> = Vec::new();
  y.push(10);
  y.push(20);
  let zipped_count: i32 = 0;
  let iter7 = x.iter().zip(y.iter());
  loop {
    match iter7.next() {
      Option::Some(_) => { zipped_count = zipped_count + 1; },
      Option::None => { break; },
    }
  }
  assert_eq!(zipped_count, 2);

  return 0;
}
