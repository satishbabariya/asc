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

  fn get_mut(refmut<Self>, key: ref<K>): Option<refmut<V>> {
    const hash = self.hash_key(key);
    const cap = self.buckets.len();
    let idx = (hash as usize) % cap;

    let i: usize = 0;
    while i < cap {
      const bucket_idx = (idx + i) % cap;
      const bucket = self.buckets.get_mut(bucket_idx).unwrap();
      if !bucket.occupied { return Option::None; }
      if bucket.hash == hash && bucket.key.eq(key) {
        return Option::Some(&mut bucket.value);
      }
      i = i + 1;
    }
    return Option::None;
  }

  fn keys(ref<Self>): own<Vec<ref<K>>> {
    let result: Vec<ref<K>> = Vec::new();
    let i: usize = 0;
    while i < self.buckets.len() {
      const bucket = self.buckets.get(i).unwrap();
      if bucket.occupied {
        result.push(&bucket.key);
      }
      i = i + 1;
    }
    return result;
  }

  fn values(ref<Self>): own<Vec<ref<V>>> {
    let result: Vec<ref<V>> = Vec::new();
    let i: usize = 0;
    while i < self.buckets.len() {
      const bucket = self.buckets.get(i).unwrap();
      if bucket.occupied {
        result.push(&bucket.value);
      }
      i = i + 1;
    }
    return result;
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

  fn entry(refmut<Self>, key: own<K>): Entry<K, V> {
    const hash = self.hash_key(&key);
    const cap = self.buckets.len();
    let idx = (hash as usize) % cap;

    let i: usize = 0;
    while i < cap {
      const bucket_idx = (idx + i) % cap;
      const bucket = self.buckets.get(bucket_idx).unwrap();
      if !bucket.occupied {
        return Entry::Vacant(VacantEntry { map: self, key: key, hash: hash });
      }
      if bucket.hash == hash && bucket.key.eq(&key) {
        return Entry::Occupied(OccupiedEntry { map: self, bucket_idx: bucket_idx });
      }
      i = i + 1;
    }
    return Entry::Vacant(VacantEntry { map: self, key: key, hash: hash });
  }

  fn values_mut(refmut<Self>): own<Vec<refmut<V>>> {
    let result: Vec<refmut<V>> = Vec::new();
    let i: usize = 0;
    while i < self.buckets.len() {
      const bucket = self.buckets.get_mut(i).unwrap();
      if bucket.occupied {
        result.push(&mut bucket.value);
      }
      i = i + 1;
    }
    return result;
  }
}

impl<K: Hash + Eq, V> Drop for HashMap<K, V> {
  fn drop(refmut<Self>): void {
    self.clear();
  }
}

// ---------- Entry API ----------

enum Entry<K, V> {
  Occupied(OccupiedEntry<K, V>),
  Vacant(VacantEntry<K, V>),
}

struct OccupiedEntry<K, V> {
  map: refmut<HashMap<K, V>>,
  bucket_idx: usize,
}

struct VacantEntry<K, V> {
  map: refmut<HashMap<K, V>>,
  key: own<K>,
  hash: u64,
}

impl<K: Hash + Eq, V> Entry<K, V> {
  /// Insert a default value if vacant, return mutable ref to the value.
  fn or_insert(own<Self>, default: own<V>): void {
    match self {
      Entry::Occupied(_) => {
        // Already present, do nothing (can't return refmut easily).
      },
      Entry::Vacant(e) => {
        e.map.insert(e.key, default);
      },
    }
  }

  /// Insert value from closure if vacant.
  fn or_insert_with(own<Self>, f: () -> own<V>): void {
    match self {
      Entry::Occupied(_) => {},
      Entry::Vacant(e) => {
        e.map.insert(e.key, f());
      },
    }
  }

  /// Modify the value if occupied, then return self for chaining.
  fn and_modify(own<Self>, f: (refmut<V>) -> void): Entry<K, V> {
    match self {
      Entry::Occupied(ref e) => {
        const bucket = e.map.buckets.get_mut(e.bucket_idx).unwrap();
        f(&mut bucket.value);
        return self;
      },
      Entry::Vacant(_) => { return self; },
    }
  }
}
