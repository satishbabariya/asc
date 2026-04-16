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

  /// Find the sorted insertion position for `key` in a node's keys array.
  /// Returns the index where `key` should be inserted to maintain sorted order.
  fn find_pos(node: ref<BTreeNode<K, V>>, key: ref<K>): usize {
    let i: usize = 0;
    while i < node.keys.len() {
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Less => { return i; },
        Ordering::Equal => { return i; },
        Ordering::Greater => { i = i + 1; },
      }
    }
    return i;
  }

  /// Insert a key-value pair. Returns the old value if the key already exists.
  fn insert(refmut<Self>, key: own<K>, value: own<V>): Option<own<V>> {
    // If no root, create a leaf with the key-value pair.
    if self.root.is_none() {
      let node = BTreeNode {
        keys: Vec::new(),
        values: Vec::new(),
        children: Vec::new(),
        is_leaf: true,
      };
      node.keys.push(key);
      node.values.push(value);
      self.root = Option::Some(node);
      self.len = self.len + 1;
      return Option::None;
    }

    // Check if root is full — if so, split it before descending.
    let root_full: bool = false;
    match self.root {
      Option::Some(ref r) => { root_full = r.keys.len() == MAX_KEYS; },
      Option::None => {},
    }
    if root_full {
      let new_root = BTreeNode {
        keys: Vec::new(),
        values: Vec::new(),
        children: Vec::new(),
        is_leaf: false,
      };
      let old_root = self.root.take().unwrap();
      new_root.children.push(old_root);
      BTreeMap::split_child(&mut new_root, 0);
      self.root = Option::Some(new_root);
    }

    // Insert into the non-full root.
    const result = BTreeMap::insert_nonfull(self.root.as_mut().unwrap(), key, value);
    match result {
      Option::None => { self.len = self.len + 1; },
      Option::Some(_) => {
        // Duplicate key — len unchanged, old value returned.
      },
    }
    return result;
  }

  /// Insert into a node that is guaranteed to have fewer than MAX_KEYS keys.
  fn insert_nonfull(node: refmut<BTreeNode<K, V>>, key: own<K>, value: own<V>): Option<own<V>> {
    let i = BTreeMap::find_pos(node, &key);

    // Check for duplicate key.
    if i < node.keys.len() {
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Equal => {
          // Replace value and return old.
          const old_value = node.values.remove(i);
          node.values.insert(i, value);
          return Option::Some(old_value);
        },
        _ => {},
      }
    }

    if node.is_leaf {
      // Insert key and value at position i.
      node.keys.insert(i, key);
      node.values.insert(i, value);
      return Option::None;
    }

    // Internal node: descend into the correct child.
    // First check if the target child is full.
    const child = node.children.get(i).unwrap();
    if child.keys.len() == MAX_KEYS {
      BTreeMap::split_child(node, i);
      // After split, the median moved up to node.keys[i].
      // Decide which child to descend into.
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Equal => {
          // The median key matches — update its value.
          const old_value = node.values.remove(i);
          node.values.insert(i, value);
          return Option::Some(old_value);
        },
        Ordering::Greater => {
          return BTreeMap::insert_nonfull(node.children.get_mut(i + 1).unwrap(), key, value);
        },
        Ordering::Less => {
          return BTreeMap::insert_nonfull(node.children.get_mut(i).unwrap(), key, value);
        },
      }
    }

    return BTreeMap::insert_nonfull(node.children.get_mut(i).unwrap(), key, value);
  }

  /// Split the child at index `child_idx` of `parent`. The child must be full (MAX_KEYS keys).
  /// Moves the median key/value up into the parent and creates a new right sibling.
  fn split_child(parent: refmut<BTreeNode<K, V>>, child_idx: usize): void {
    const mid = B - 1; // Index of the median key (5 for B=6).
    const child = parent.children.get_mut(child_idx).unwrap();

    // Create the new right sibling.
    let right = BTreeNode {
      keys: Vec::new(),
      values: Vec::new(),
      children: Vec::new(),
      is_leaf: child.is_leaf,
    };

    // Move keys[mid+1..] and values[mid+1..] from child to right.
    // Always remove from index mid+1 since elements shift left after each remove.
    const total_keys = child.keys.len();
    const right_count = total_keys - mid - 1;

    // Move keys[mid+1..] to right.
    let r: usize = 0;
    while r < right_count {
      const moved_key = child.keys.remove(mid + 1);
      const moved_val = child.values.remove(mid + 1);
      right.keys.push(moved_key);
      right.values.push(moved_val);
      r = r + 1;
    }

    // Move children[mid+1..] to right (if not leaf).
    if !child.is_leaf {
      const child_count = child.children.len();
      const right_children = child_count - mid - 1;
      let c: usize = 0;
      while c < right_children {
        const moved_child = child.children.remove(mid + 1);
        right.children.push(moved_child);
        c = c + 1;
      }
    }

    // Extract the median key/value from child.
    const median_key = child.keys.remove(mid);
    const median_val = child.values.remove(mid);

    // Insert median into parent at child_idx.
    parent.keys.insert(child_idx, median_key);
    parent.values.insert(child_idx, median_val);

    // Insert the new right node as parent's child at child_idx + 1.
    parent.children.insert(child_idx + 1, right);
  }

  /// Remove a key from the map. Returns the old value if found.
  fn remove(refmut<Self>, key: ref<K>): Option<own<V>> {
    if self.root.is_none() { return Option::None; }

    const result = BTreeMap::remove_from_node(self.root.as_mut().unwrap(), key);
    match result {
      Option::Some(_) => {
        self.len = self.len - 1;
        // If root has no keys and has a child, promote the child.
        let should_promote: bool = false;
        match self.root {
          Option::Some(ref r) => {
            should_promote = r.keys.is_empty() && !r.is_leaf;
          },
          Option::None => {},
        }
        if should_promote {
          let old_root = self.root.take().unwrap();
          const child = old_root.children.remove(0);
          self.root = Option::Some(child);
        }
        return result;
      },
      Option::None => { return Option::None; },
    }
  }

  /// Remove a key from the given node (recursive).
  fn remove_from_node(node: refmut<BTreeNode<K, V>>, key: ref<K>): Option<own<V>> {
    // Find the position where key would be.
    let i: usize = 0;
    let found: bool = false;
    while i < node.keys.len() {
      match key.cmp(node.keys.get(i).unwrap()) {
        Ordering::Equal => { found = true; break; },
        Ordering::Less => { break; },
        Ordering::Greater => { i = i + 1; },
      }
    }

    if found {
      if node.is_leaf {
        // Simple case: remove from leaf.
        const removed_key = node.keys.remove(i);
        const removed_val = node.values.remove(i);
        return Option::Some(removed_val);
      }

      // Internal node: replace with in-order predecessor.
      // The predecessor is the rightmost key in the left subtree (children[i]).
      const left_child = node.children.get_mut(i).unwrap();
      const pred = BTreeMap::remove_largest(left_child);
      // Replace node.keys[i] and node.values[i] with predecessor.
      const old_key = node.keys.remove(i);
      const old_val = node.values.remove(i);
      node.keys.insert(i, pred.0);
      node.values.insert(i, pred.1);
      return Option::Some(old_val);
    }

    // Key not found in this node.
    if node.is_leaf {
      return Option::None;
    }

    // Descend into children[i].
    return BTreeMap::remove_from_node(node.children.get_mut(i).unwrap(), key);
  }

  /// Remove and return the largest (rightmost) key-value pair from a subtree.
  fn remove_largest(node: refmut<BTreeNode<K, V>>): (own<K>, own<V>) {
    if node.is_leaf {
      const last_idx = node.keys.len() - 1;
      const k = node.keys.remove(last_idx);
      const v = node.values.remove(last_idx);
      return (k, v);
    }
    // Descend into the rightmost child.
    const last_child_idx = node.children.len() - 1;
    return BTreeMap::remove_largest(node.children.get_mut(last_child_idx).unwrap());
  }

  fn contains_key(ref<Self>, key: ref<K>): bool {
    return self.get(key).is_some();
  }

  /// Return a reference to the first (smallest) key-value pair.
  fn first_key_value(ref<Self>): Option<(ref<K>, ref<V>)> {
    match self.root {
      Option::None => { return Option::None; },
      Option::Some(ref node) => { return BTreeMap::leftmost(node); },
    }
  }

  /// Walk to the leftmost leaf and return a reference to the first key-value.
  fn leftmost(node: ref<BTreeNode<K, V>>): Option<(ref<K>, ref<V>)> {
    if node.keys.is_empty() { return Option::None; }
    if node.is_leaf {
      const k = node.keys.get(0).unwrap();
      const v = node.values.get(0).unwrap();
      return Option::Some((k, v));
    }
    return BTreeMap::leftmost(node.children.get(0).unwrap());
  }

  /// Return a reference to the last (largest) key-value pair.
  fn last_key_value(ref<Self>): Option<(ref<K>, ref<V>)> {
    match self.root {
      Option::None => { return Option::None; },
      Option::Some(ref node) => { return BTreeMap::rightmost(node); },
    }
  }

  /// Walk to the rightmost leaf and return a reference to the last key-value.
  fn rightmost(node: ref<BTreeNode<K, V>>): Option<(ref<K>, ref<V>)> {
    if node.keys.is_empty() { return Option::None; }
    if node.is_leaf {
      const last = node.keys.len() - 1;
      const k = node.keys.get(last).unwrap();
      const v = node.values.get(last).unwrap();
      return Option::Some((k, v));
    }
    const last_child = node.children.len() - 1;
    return BTreeMap::rightmost(node.children.get(last_child).unwrap());
  }

  fn clear(refmut<Self>): void {
    self.root = Option::None;
    self.len = 0;
  }
}
