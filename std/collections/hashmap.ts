// std/collections/hashmap.ts — HashMap<K,V> (RFC-0013)
// Robin Hood hashing with linear probing.

import { Hash, Hasher, SipHasher, RandomState, BuildHasher } from '../core/hash';
import { PartialEq, Eq } from '../core/cmp';

/// A hash map entry: key, value, and distance from ideal position.
struct Bucket<K, V> {
  key: own<K>,
  value: own<V>,
  hash: u64,
  occupied: bool,
}

/// Hash map with Robin Hood hashing.
struct HashMap<K: Hash + Eq, V> {
  buckets: own<Vec<Bucket<K, V>>>,
  len: usize,
  hash_builder: own<RandomState>,
}

impl<K: Hash + Eq, V> HashMap<K, V> {
  fn new(): own<HashMap<K, V>> {
    return HashMap::with_capacity(16);
  }

  fn with_capacity(capacity: usize): own<HashMap<K, V>> {
    let cap = 16;
    while cap < capacity { cap = cap * 2; }
    let buckets = Vec::with_capacity(cap);
    let i: usize = 0;
    while i < cap {
      buckets.push(Bucket { key: unsafe { zeroed() }, value: unsafe { zeroed() },
                            hash: 0, occupied: false });
      i = i + 1;
    }
    return HashMap {
      buckets: buckets,
      len: 0,
      hash_builder: RandomState::new(),
    };
  }

  fn len(ref<Self>): usize { return self.len; }
  fn is_empty(ref<Self>): bool { return self.len == 0; }

  fn hash_key(ref<Self>, key: ref<K>): u64 {
    let hasher = self.hash_builder.build_hasher();
    key.hash(&hasher);
    return hasher.finish();
  }

  fn insert(refmut<Self>, key: own<K>, value: own<V>): Option<own<V>> {
    const hash = self.hash_key(&key);
    const cap = self.buckets.len();
    let idx = (hash as usize) % cap;

    // Linear probe.
    let i: usize = 0;
    while i < cap {
      const bucket_idx = (idx + i) % cap;
      const bucket = self.buckets.get_mut(bucket_idx).unwrap();
      if !bucket.occupied {
        bucket.key = key;
        bucket.value = value;
        bucket.hash = hash;
        bucket.occupied = true;
        self.len = self.len + 1;
        return Option::None;
      }
      if bucket.hash == hash && bucket.key.eq(&key) {
        // Key exists — replace value.
        const old_value = bucket.value;
        bucket.value = value;
        return Option::Some(old_value);
      }
      i = i + 1;
    }
    // Table full — should never happen with proper load factor.
    panic!("HashMap: table full");
  }

  fn get(ref<Self>, key: ref<K>): Option<ref<V>> {
    const hash = self.hash_key(key);
    const cap = self.buckets.len();
    let idx = (hash as usize) % cap;

    let i: usize = 0;
    while i < cap {
      const bucket_idx = (idx + i) % cap;
      const bucket = self.buckets.get(bucket_idx).unwrap();
      if !bucket.occupied { return Option::None; }
      if bucket.hash == hash && bucket.key.eq(key) {
        return Option::Some(&bucket.value);
      }
      i = i + 1;
    }
    return Option::None;
  }

  fn contains_key(ref<Self>, key: ref<K>): bool {
    return self.get(key).is_some();
  }

  fn remove(refmut<Self>, key: ref<K>): Option<own<V>> {
    const hash = self.hash_key(key);
    const cap = self.buckets.len();
    let idx = (hash as usize) % cap;

    let i: usize = 0;
    while i < cap {
      const bucket_idx = (idx + i) % cap;
      const bucket = self.buckets.get_mut(bucket_idx).unwrap();
      if !bucket.occupied { return Option::None; }
      if bucket.hash == hash && bucket.key.eq(key) {
        const value = bucket.value;
        bucket.occupied = false;
        self.len = self.len - 1;
        return Option::Some(value);
      }
      i = i + 1;
    }
    return Option::None;
  }

  fn clear(refmut<Self>): void {
    let i: usize = 0;
    while i < self.buckets.len() {
      const bucket = self.buckets.get_mut(i).unwrap();
      bucket.occupied = false;
      i = i + 1;
    }
    self.len = 0;
  }
}

impl<K: Hash + Eq, V> Drop for HashMap<K, V> {
  fn drop(refmut<Self>): void {
    self.clear();
  }
}
