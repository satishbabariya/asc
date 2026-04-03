// std/collections/vecdeque.ts — VecDeque<T> (RFC-0013)
// Double-ended queue backed by a ring buffer.

struct VecDeque<T> {
  buf: *mut T,
  head: usize,
  len: usize,
  cap: usize,
}

impl<T> VecDeque<T> {
  fn new(): own<VecDeque<T>> {
    return VecDeque { buf: null, head: 0, len: 0, cap: 0 };
  }

  fn with_capacity(capacity: usize): own<VecDeque<T>> {
    let cap = 4;
    while cap < capacity { cap = cap * 2; }
    const elem_size = size_of!<T>();
    const buf = malloc(cap * elem_size) as *mut T;
    return VecDeque { buf: buf, head: 0, len: 0, cap: cap };
  }

  fn len(ref<Self>): usize { return self.len; }
  fn is_empty(ref<Self>): bool { return self.len == 0; }

  fn push_back(refmut<Self>, value: own<T>): void {
    if self.len == self.cap { self.grow(); }
    const idx = (self.head + self.len) % self.cap;
    const elem_size = size_of!<T>();
    const slot = (self.buf as usize + idx * elem_size) as *mut T;
    unsafe { ptr_write(slot, value); }
    self.len = self.len + 1;
  }

  fn push_front(refmut<Self>, value: own<T>): void {
    if self.len == self.cap { self.grow(); }
    self.head = if self.head == 0 { self.cap - 1 } else { self.head - 1 };
    const elem_size = size_of!<T>();
    const slot = (self.buf as usize + self.head * elem_size) as *mut T;
    unsafe { ptr_write(slot, value); }
    self.len = self.len + 1;
  }

  fn pop_back(refmut<Self>): Option<own<T>> {
    if self.len == 0 { return Option::None; }
    self.len = self.len - 1;
    const idx = (self.head + self.len) % self.cap;
    const elem_size = size_of!<T>();
    const slot = (self.buf as usize + idx * elem_size) as *const T;
    return Option::Some(unsafe { ptr_read(slot) });
  }

  fn pop_front(refmut<Self>): Option<own<T>> {
    if self.len == 0 { return Option::None; }
    const elem_size = size_of!<T>();
    const slot = (self.buf as usize + self.head * elem_size) as *const T;
    const value = unsafe { ptr_read(slot) };
    self.head = (self.head + 1) % self.cap;
    self.len = self.len - 1;
    return Option::Some(value);
  }

  fn front(ref<Self>): Option<ref<T>> {
    if self.len == 0 { return Option::None; }
    const elem_size = size_of!<T>();
    const slot = (self.buf as usize + self.head * elem_size) as *const T;
    return Option::Some(unsafe { &*slot });
  }

  fn back(ref<Self>): Option<ref<T>> {
    if self.len == 0 { return Option::None; }
    const idx = (self.head + self.len - 1) % self.cap;
    const elem_size = size_of!<T>();
    const slot = (self.buf as usize + idx * elem_size) as *const T;
    return Option::Some(unsafe { &*slot });
  }

  fn grow(refmut<Self>): void {
    let new_cap = self.cap * 2;
    if new_cap < 4 { new_cap = 4; }
    const elem_size = size_of!<T>();
    const new_buf = malloc(new_cap * elem_size) as *mut T;
    // Copy elements in order.
    let i: usize = 0;
    while i < self.len {
      const src_idx = (self.head + i) % self.cap;
      const src = (self.buf as usize + src_idx * elem_size) as *const u8;
      const dst = (new_buf as usize + i * elem_size) as *mut u8;
      memcpy(dst, src, elem_size);
      i = i + 1;
    }
    if self.buf != null { free(self.buf); }
    self.buf = new_buf;
    self.head = 0;
    self.cap = new_cap;
  }
}

impl<T> Drop for VecDeque<T> {
  fn drop(refmut<Self>): void {
    while self.pop_front().is_some() {}
    if self.buf != null { free(self.buf); }
  }
}
