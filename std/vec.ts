// std/vec.ts — Vec<T> and slice methods (RFC-0013)

/// Growable array. Layout: { ptr: *mut T, len: usize, cap: usize }.
struct Vec<T> {
  ptr: *mut T,
  len: usize,
  cap: usize,
}

impl<T> Vec<T> {
  fn new(): own<Vec<T>> {
    return Vec { ptr: null, len: 0, cap: 0 };
  }

  fn with_capacity(capacity: usize): own<Vec<T>> {
    const elem_size = size_of!<T>();
    const ptr = malloc(capacity * elem_size) as *mut T;
    return Vec { ptr: ptr, len: 0, cap: capacity };
  }

  fn len(ref<Self>): usize { return self.len; }
  fn is_empty(ref<Self>): bool { return self.len == 0; }
  fn capacity(ref<Self>): usize { return self.cap; }

  fn push(refmut<Self>, value: own<T>): void {
    if self.len == self.cap { self.grow(); }
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + self.len * elem_size) as *mut T;
    unsafe { ptr_write(slot, value); }
    self.len = self.len + 1;
  }

  fn pop(refmut<Self>): Option<own<T>> {
    if self.len == 0 { return Option::None; }
    self.len = self.len - 1;
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + self.len * elem_size) as *const T;
    return Option::Some(unsafe { ptr_read(slot) });
  }

  fn get(ref<Self>, index: usize): Option<ref<T>> {
    if index >= self.len { return Option::None; }
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + index * elem_size) as *const T;
    return Option::Some(unsafe { &*slot });
  }

  fn get_mut(refmut<Self>, index: usize): Option<refmut<T>> {
    if index >= self.len { return Option::None; }
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + index * elem_size) as *mut T;
    return Option::Some(unsafe { &mut *slot });
  }

  fn first(ref<Self>): Option<ref<T>> { return self.get(0); }
  fn last(ref<Self>): Option<ref<T>> {
    if self.len == 0 { return Option::None; }
    return self.get(self.len - 1);
  }

  fn clear(refmut<Self>): void {
    // Drop all elements.
    let i: usize = 0;
    while i < self.len {
      const elem_size = size_of!<T>();
      const slot = (self.ptr as usize + i * elem_size) as *mut T;
      unsafe { ptr_drop_in_place(slot); }
      i = i + 1;
    }
    self.len = 0;
  }

  fn insert(refmut<Self>, index: usize, value: own<T>): void {
    assert!(index <= self.len);
    if self.len == self.cap { self.grow(); }
    const elem_size = size_of!<T>();
    // Shift elements right.
    if index < self.len {
      const src = (self.ptr as usize + index * elem_size) as *const u8;
      const dst = (self.ptr as usize + (index + 1) * elem_size) as *mut u8;
      const count = (self.len - index) * elem_size;
      memmove(dst, src, count);
    }
    const slot = (self.ptr as usize + index * elem_size) as *mut T;
    unsafe { ptr_write(slot, value); }
    self.len = self.len + 1;
  }

  fn remove(refmut<Self>, index: usize): own<T> {
    assert!(index < self.len);
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + index * elem_size) as *const T;
    const value = unsafe { ptr_read(slot) };
    // Shift elements left.
    if index + 1 < self.len {
      const src = (self.ptr as usize + (index + 1) * elem_size) as *const u8;
      const dst = (self.ptr as usize + index * elem_size) as *mut u8;
      const count = (self.len - index - 1) * elem_size;
      memmove(dst, src, count);
    }
    self.len = self.len - 1;
    return value;
  }

  fn swap_remove(refmut<Self>, index: usize): own<T> {
    assert!(index < self.len);
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + index * elem_size) as *mut T;
    const last = (self.ptr as usize + (self.len - 1) * elem_size) as *mut T;
    const value = unsafe { ptr_read(slot) };
    if index != self.len - 1 {
      unsafe { ptr_write(slot, ptr_read(last)); }
    }
    self.len = self.len - 1;
    return value;
  }

  fn contains(ref<Self>, value: ref<T>): bool where T: PartialEq {
    let i: usize = 0;
    while i < self.len {
      const elem_size = size_of!<T>();
      const slot = (self.ptr as usize + i * elem_size) as *const T;
      if unsafe { &*slot }.eq(value) { return true; }
      i = i + 1;
    }
    return false;
  }

  fn reverse(refmut<Self>): void {
    if self.len <= 1 { return; }
    let lo: usize = 0;
    let hi: usize = self.len - 1;
    const elem_size = size_of!<T>();
    while lo < hi {
      const a = (self.ptr as usize + lo * elem_size) as *mut u8;
      const b = (self.ptr as usize + hi * elem_size) as *mut u8;
      // Swap elem_size bytes.
      let k: usize = 0;
      while k < elem_size {
        const tmp = a[k];
        a[k] = b[k];
        b[k] = tmp;
        k = k + 1;
      }
      lo = lo + 1;
      hi = hi - 1;
    }
  }

  fn extend(refmut<Self>, other: own<Vec<T>>): void {
    let i: usize = 0;
    while i < other.len {
      const elem_size = size_of!<T>();
      const slot = (other.ptr as usize + i * elem_size) as *const T;
      self.push(unsafe { ptr_read(slot) });
      i = i + 1;
    }
    // Don't drop other's elements since we moved them.
    free(other.ptr);
  }

  // Internal: double capacity.
  fn grow(refmut<Self>): void {
    let new_cap = self.cap * 2;
    if new_cap < 4 { new_cap = 4; }
    const elem_size = size_of!<T>();
    const new_ptr = malloc(new_cap * elem_size) as *mut T;
    if self.ptr != null {
      memcpy(new_ptr as *mut u8, self.ptr as *const u8, self.len * elem_size);
      free(self.ptr);
    }
    self.ptr = new_ptr;
    self.cap = new_cap;
  }
}

impl<T> Drop for Vec<T> {
  fn drop(refmut<Self>): void {
    self.clear();
    if self.ptr != null { free(self.ptr); }
  }
}

impl<T> Index<usize> for Vec<T> {
  type Output = T;
  fn index(ref<Self>, idx: usize): ref<T> {
    assert!(idx < self.len);
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + idx * elem_size) as *const T;
    return unsafe { &*slot };
  }
}

impl<T> IndexMut<usize> for Vec<T> {
  fn index_mut(refmut<Self>, idx: usize): refmut<T> {
    assert!(idx < self.len);
    const elem_size = size_of!<T>();
    const slot = (self.ptr as usize + idx * elem_size) as *mut T;
    return unsafe { &mut *slot };
  }
}

impl<T: Clone> Clone for Vec<T> {
  fn clone(ref<Self>): own<Vec<T>> {
    let result = Vec::with_capacity(self.len);
    let i: usize = 0;
    while i < self.len {
      const elem_size = size_of!<T>();
      const slot = (self.ptr as usize + i * elem_size) as *const T;
      result.push(unsafe { &*slot }.clone());
      i = i + 1;
    }
    return result;
  }
}
