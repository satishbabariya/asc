// std/collections/btreeset.ts — BTreeSet<T> (RFC-0013)

import { Ord } from '../core/cmp';

struct BTreeSet<T: Ord> {
  map: own<BTreeMap<T, ()>>,
}

impl<T: Ord> BTreeSet<T> {
  fn new(): own<BTreeSet<T>> { return BTreeSet { map: BTreeMap::new() }; }
  fn len(ref<Self>): usize { return self.map.len(); }
  fn is_empty(ref<Self>): bool { return self.map.is_empty(); }
  fn insert(refmut<Self>, value: own<T>): bool {
    return self.map.insert(value, ()).is_none();
  }
  fn contains(ref<Self>, value: ref<T>): bool {
    return self.map.contains_key(value);
  }
  fn clear(refmut<Self>): void { self.map.clear(); }
}
