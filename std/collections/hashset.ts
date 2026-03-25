// std/collections/hashset.ts — HashSet<T> (RFC-0013)
// Wrapper around HashMap<T, ()>.

import { Hash } from '../core/hash';
import { Eq } from '../core/cmp';

struct HashSet<T: Hash + Eq> {
  map: own<HashMap<T, ()>>,
}

impl<T: Hash + Eq> HashSet<T> {
  fn new(): own<HashSet<T>> { return HashSet { map: HashMap::new() }; }
  fn with_capacity(cap: usize): own<HashSet<T>> {
    return HashSet { map: HashMap::with_capacity(cap) };
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
  fn clear(refmut<Self>): void { self.map.clear(); }
}
