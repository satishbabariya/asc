// std/collections/linked_list.ts — LinkedList<T> (RFC-0013)
// Doubly-linked list with O(1) push/pop at both ends.

struct Node<T> {
  value: own<T>,
  prev: *mut Node<T>,
  next: *mut Node<T>,
}

/// Doubly-linked list.
struct LinkedList<T> {
  head: *mut Node<T>,
  tail: *mut Node<T>,
  len: usize,
}

impl<T> LinkedList<T> {
  fn new(): own<LinkedList<T>> {
    return LinkedList { head: null, tail: null, len: 0 };
  }

  fn len(ref<Self>): usize { return self.len; }
  fn is_empty(ref<Self>): bool { return self.len == 0; }

  fn push_back(refmut<Self>, value: own<T>): void {
    const node_size = size_of!<Node<T>>();
    const node = malloc(node_size) as *mut Node<T>;
    unsafe {
      ptr_write(&mut (*node).value, value);
      (*node).prev = self.tail;
      (*node).next = null;
    }
    if self.tail != null {
      unsafe { (*self.tail).next = node; }
    } else {
      self.head = node;
    }
    self.tail = node;
    self.len = self.len + 1;
  }

  fn push_front(refmut<Self>, value: own<T>): void {
    const node_size = size_of!<Node<T>>();
    const node = malloc(node_size) as *mut Node<T>;
    unsafe {
      ptr_write(&mut (*node).value, value);
      (*node).prev = null;
      (*node).next = self.head;
    }
    if self.head != null {
      unsafe { (*self.head).prev = node; }
    } else {
      self.tail = node;
    }
    self.head = node;
    self.len = self.len + 1;
  }

  fn pop_back(refmut<Self>): Option<own<T>> {
    if self.tail == null { return Option::None; }
    const node = self.tail;
    const value = unsafe { ptr_read(&(*node).value) };
    const prev = unsafe { (*node).prev };
    if prev != null {
      unsafe { (*prev).next = null; }
    } else {
      self.head = null;
    }
    self.tail = prev;
    self.len = self.len - 1;
    free(node);
    return Option::Some(value);
  }

  fn pop_front(refmut<Self>): Option<own<T>> {
    if self.head == null { return Option::None; }
    const node = self.head;
    const value = unsafe { ptr_read(&(*node).value) };
    const next = unsafe { (*node).next };
    if next != null {
      unsafe { (*next).prev = null; }
    } else {
      self.tail = null;
    }
    self.head = next;
    self.len = self.len - 1;
    free(node);
    return Option::Some(value);
  }

  fn front(ref<Self>): Option<ref<T>> {
    if self.head == null { return Option::None; }
    return Option::Some(unsafe { &(*self.head).value });
  }

  fn back(ref<Self>): Option<ref<T>> {
    if self.tail == null { return Option::None; }
    return Option::Some(unsafe { &(*self.tail).value });
  }

  fn clear(refmut<Self>): void {
    while self.pop_front().is_some() {}
  }

  fn contains(ref<Self>, value: ref<T>): bool where T: PartialEq {
    let current = self.head;
    while current != null {
      if unsafe { &(*current).value }.eq(value) { return true; }
      current = unsafe { (*current).next };
    }
    return false;
  }
}

impl<T> Drop for LinkedList<T> {
  fn drop(refmut<Self>): void {
    self.clear();
  }
}
