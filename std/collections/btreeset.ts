// std/collections/btreeset.ts — BTreeSet<T> (RFC-0013)
// Ordered set backed by a BTreeMap<T, ()>.

import { Ord } from '../core/cmp';

struct BTreeSet<T: Ord> {
  map: own<BTreeMap<T, ()>>,
}

impl<T: Ord> BTreeSet<T> {
  fn new(): own<BTreeSet<T>> {
    return BTreeSet { map: BTreeMap::new() };
  }

  fn len(ref<Self>): usize { return self.map.len(); }
  fn is_empty(ref<Self>): bool { return self.map.is_empty(); }

  fn insert(refmut<Self>, value: own<T>): bool {
    return self.map.insert(value, ()).is_none();
  }

  fn contains(ref<Self>, value: ref<T>): bool {
    return self.map.contains_key(value);
  }

  fn remove(refmut<Self>, value: ref<T>): bool {
    return self.map.remove(value).is_some();
  }

  fn first(ref<Self>): Option<ref<T>> {
    match self.map.first_key_value() {
      Option::Some(kv) => { return Option::Some(kv.0); },
      Option::None => { return Option::None; },
    }
  }

  fn last(ref<Self>): Option<ref<T>> {
    match self.map.last_key_value() {
      Option::Some(kv) => { return Option::Some(kv.0); },
      Option::None => { return Option::None; },
    }
  }

  fn clear(refmut<Self>): void { self.map.clear(); }
}
