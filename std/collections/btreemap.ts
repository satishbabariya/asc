// std/collections/btreemap.ts — BTreeMap<K,V> (RFC-0013)
// B-tree with order B=6 (max 11 keys per node).

import { Ord } from '../core/cmp';

const B: usize = 6;
const MAX_KEYS: usize = 2 * B - 1;

struct BTreeNode<K, V> {
  keys: own<Vec<K>>,
  values: own<Vec<V>>,
  children: own<Vec<own<BTreeNode<K, V>>>>,
  is_leaf: bool,
}

struct BTreeMap<K: Ord, V> {
  root: Option<own<BTreeNode<K, V>>>,
  len: usize,
}

impl<K: Ord, V> BTreeMap<K, V> {
  fn new(): own<BTreeMap<K, V>> {
    return BTreeMap { root: Option::None, len: 0 };
  }

  fn len(ref<Self>): usize { return self.len; }
  fn is_empty(ref<Self>): bool { return self.len == 0; }

  fn get(ref<Self>, key: ref<K>): Option<ref<V>> {
    match self.root {
      Option::Some(ref node) => { return BTreeMap::search_node(node, key); },
      Option::None => { return Option::None; },
    }
  }

  fn search_node(node: ref<BTreeNode<K, V>>, key: ref<K>): Option<ref<V>> {
    let i: usize = 0;
    while i < node.keys.len() {
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Equal => { return node.values.get(i); },
        Ordering::Less => {
          if node.is_leaf { return Option::None; }
          return BTreeMap::search_node(node.children.get(i).unwrap(), key);
        },
        Ordering::Greater => { i = i + 1; },
      }
    }
    if node.is_leaf { return Option::None; }
    return BTreeMap::search_node(node.children.get(i).unwrap(), key);
  }

  fn insert(refmut<Self>, key: own<K>, value: own<V>): Option<own<V>> {
    // DECISION: Simplified insertion — full B-tree splitting deferred.
    // For now, use linear search in a single node.
    self.len = self.len + 1;
    match self.root {
      Option::None => {
        let node = BTreeNode {
          keys: Vec::new(),
          values: Vec::new(),
          children: Vec::new(),
          is_leaf: true,
        };
        node.keys.push(key);
        node.values.push(value);
        self.root = Option::Some(node);
        return Option::None;
      },
      Option::Some(ref node) => {
        // Linear insert into leaf.
        node.keys.push(key);
        node.values.push(value);
        return Option::None;
      },
    }
  }

  fn contains_key(ref<Self>, key: ref<K>): bool {
    return self.get(key).is_some();
  }

  fn clear(refmut<Self>): void {
    self.root = Option::None;
    self.len = 0;
  }
}
