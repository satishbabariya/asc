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

  /// Returns a new set containing elements in both self and other.
  fn intersection(ref<Self>, other: ref<HashSet<T>>): own<HashSet<T>> {
    let result = HashSet::new();
    let ks = self.map.keys();
    let i: usize = 0;
    while i < ks.len() {
      const k = *ks.get(i).unwrap();
      if other.contains(k) {
        // Clone the key to insert into result.
        result.insert(k.clone());
      }
      i = i + 1;
    }
    return result;
  }

  /// Returns a new set containing elements in self but not in other.
  fn difference(ref<Self>, other: ref<HashSet<T>>): own<HashSet<T>> {
    let result = HashSet::new();
    let ks = self.map.keys();
    let i: usize = 0;
    while i < ks.len() {
      const k = *ks.get(i).unwrap();
      if !other.contains(k) {
        result.insert(k.clone());
      }
      i = i + 1;
    }
    return result;
  }

  /// Returns a new set containing elements in either self or other.
  fn union_with(ref<Self>, other: ref<HashSet<T>>): own<HashSet<T>> {
    let result = HashSet::new();
    let ks = self.map.keys();
    let i: usize = 0;
    while i < ks.len() {
      result.insert((*ks.get(i).unwrap()).clone());
      i = i + 1;
    }
    let oks = other.map.keys();
    let j: usize = 0;
    while j < oks.len() {
      const k = *oks.get(j).unwrap();
      if !result.contains(k) {
        result.insert(k.clone());
      }
      j = j + 1;
    }
    return result;
  }

  /// Returns a new set containing elements in exactly one of self or other.
  fn symmetric_difference(ref<Self>, other: ref<HashSet<T>>): own<HashSet<T>> {
    let result = HashSet::new();
    let ks = self.map.keys();
    let i: usize = 0;
    while i < ks.len() {
      const k = *ks.get(i).unwrap();
      if !other.contains(k) {
        result.insert(k.clone());
      }
      i = i + 1;
    }
    let oks = other.map.keys();
    let j: usize = 0;
    while j < oks.len() {
      const k = *oks.get(j).unwrap();
      if !self.contains(k) {
        result.insert(k.clone());
      }
      j = j + 1;
    }
    return result;
  }

  /// Returns true if self is a subset of other.
  fn is_subset(ref<Self>, other: ref<HashSet<T>>): bool {
    let ks = self.map.keys();
    let i: usize = 0;
    while i < ks.len() {
      if !other.contains(*ks.get(i).unwrap()) { return false; }
      i = i + 1;
    }
    return true;
  }

  /// Returns true if self is a superset of other.
  fn is_superset(ref<Self>, other: ref<HashSet<T>>): bool {
    return other.is_subset(self);
  }
}
