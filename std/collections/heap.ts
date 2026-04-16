// std/collections/heap.ts — BinaryHeap<T> (RFC-0013)
// Max-heap backed by a Vec<T>.

import { Ord } from '../core/cmp';

/// Binary max-heap.
struct BinaryHeap<T: Ord> {
  data: own<Vec<T>>,
}

impl<T: Ord> BinaryHeap<T> {
  fn new(): own<BinaryHeap<T>> {
    return BinaryHeap { data: Vec::new() };
  }

  fn len(ref<Self>): usize { return self.data.len(); }
  fn is_empty(ref<Self>): bool { return self.data.is_empty(); }

  /// Returns a reference to the maximum element (the root).
  fn peek(ref<Self>): Option<ref<T>> {
    return self.data.first();
  }

  /// Push a value onto the heap.
  fn push(refmut<Self>, value: own<T>): void {
    self.data.push(value);
    self.sift_up(self.data.len() - 1);
  }

  /// Remove and return the maximum element.
  fn pop(refmut<Self>): Option<own<T>> {
    if self.data.is_empty() { return Option::None; }
    const last_idx = self.data.len() - 1;
    self.swap(0, last_idx);
    const value = self.data.pop().unwrap();
    if !self.data.is_empty() {
      self.sift_down(0);
    }
    return Option::Some(value);
  }

  /// Consume the heap and return a sorted Vec (ascending order).
  fn into_sorted(refmut<Self>): own<Vec<T>> {
    let result = Vec::new();
    // Pop all elements (max first) then reverse for ascending.
    let items = Vec::new();
    while !self.data.is_empty() {
      items.push(self.pop().unwrap());
    }
    // items is in descending order; reverse for ascending.
    let i = items.len();
    while i > 0 {
      i = i - 1;
      const elem_size = size_of!<T>();
      const slot = (items.ptr as usize + i * elem_size) as *const T;
      result.push(unsafe { ptr_read(slot) });
    }
    // Prevent double-drop of moved elements.
    free(items.ptr);
    return result;
  }

  // Bubble element at `idx` up to restore heap property.
  fn sift_up(refmut<Self>, idx: usize): void {
    let i = idx;
    while i > 0 {
      const parent = (i - 1) / 2;
      const child_ref = self.data.get(i).unwrap();
      const parent_ref = self.data.get(parent).unwrap();
      match child_ref.cmp(parent_ref) {
        Ordering::Greater => {
          self.swap(i, parent);
          i = parent;
        },
        _ => { return; },
      }
    }
  }

  // Push element at `idx` down to restore heap property.
  fn sift_down(refmut<Self>, idx: usize): void {
    const len = self.data.len();
    let i = idx;
    while true {
      let largest = i;
      const left = 2 * i + 1;
      const right = 2 * i + 2;

      if left < len {
        match self.data.get(left).unwrap().cmp(self.data.get(largest).unwrap()) {
          Ordering::Greater => { largest = left; },
          _ => {},
        }
      }
      if right < len {
        match self.data.get(right).unwrap().cmp(self.data.get(largest).unwrap()) {
          Ordering::Greater => { largest = right; },
          _ => {},
        }
      }

      if largest == i { return; }
      self.swap(i, largest);
      i = largest;
    }
  }

  // Swap two elements in the backing Vec.
  fn swap(refmut<Self>, a: usize, b: usize): void {
    if a == b { return; }
    const elem_size = size_of!<T>();
    const ptr_a = (self.data.ptr as usize + a * elem_size) as *mut u8;
    const ptr_b = (self.data.ptr as usize + b * elem_size) as *mut u8;
    let k: usize = 0;
    while k < elem_size {
      const tmp = ptr_a[k];
      ptr_a[k] = ptr_b[k];
      ptr_b[k] = tmp;
      k = k + 1;
    }
  }
}

impl<T: Ord> Drop for BinaryHeap<T> {
  fn drop(refmut<Self>): void {
    // Vec's drop handles element cleanup.
  }
}
